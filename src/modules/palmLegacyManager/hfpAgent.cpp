// Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// SPDX-License-Identifier: Apache-2.0

#include "hfpAgent.h"

#include <cstring>
#include <unistd.h>

#include <gio/gunixfdlist.h>
#include <glib-unix.h>

namespace {

const char OFONO_SERVICE[] = "org.ofono";
const char MANAGER_IFACE[] = "org.ofono.HandsfreeAudioManager";
const char CARD_IFACE[] = "org.ofono.HandsfreeAudioCard";

/* HFP 1.6 codec ids, as oFono numbers them. */
const guchar CODEC_CVSD = 1;

const char AGENT_XML[] =
    "<node>"
    "  <interface name='org.ofono.HandsfreeAudioAgent'>"
    "    <method name='NewConnection'>"
    "      <arg type='o' name='card' direction='in'/>"
    "      <arg type='h' name='sco' direction='in'/>"
    "      <arg type='y' name='codec' direction='in'/>"
    "    </method>"
    "    <method name='Release'/>"
    "  </interface>"
    "</node>";

GDBusNodeInfo *agentInfo()
{
    static GDBusNodeInfo *info = g_dbus_node_info_new_for_xml(AGENT_XML, nullptr);
    return info;
}

bool cancelled(const GError *error)
{
    return error && g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
}

}

HfpAgent::HfpAgent(Listener &listener, Log log) :
    mListener(listener),
    mLog(std::move(log))
{
}

HfpAgent::~HfpAgent()
{
    stop();
}

void HfpAgent::log(bool error, const std::string &message)
{
    if (mLog)
        mLog(error, message);
}

bool HfpAgent::parseCard(GVariant *properties, std::string *remote)
{
    if (!properties || !g_variant_is_of_type(properties, G_VARIANT_TYPE("a{sv}")))
        return false;

    const gchar *type = nullptr;
    const gchar *address = nullptr;
    /* hfp_ag_bluez5 makes its cards "gateway"; "handsfree" ones are the
     * other direction, this phone as a car kit for another phone. */
    if (!g_variant_lookup(properties, "Type", "&s", &type) || g_strcmp0(type, "gateway") != 0)
        return false;

    if (remote)
        *remote = g_variant_lookup(properties, "RemoteAddress", "&s", &address) && address
                ? address : "";
    return true;
}

bool HfpAgent::start(GDBusConnection *bus)
{
    if (mCancel)
        return true;    /* already started */

    mContext = g_main_context_ref_thread_default();
    mCancel = g_cancellable_new();

    if (!agentInfo())
    {
        log(true, "HFP agent: bad introspection data");
        return false;
    }

    if (bus)
    {
        attach(bus);
        return true;
    }

    g_bus_get(G_BUS_TYPE_SYSTEM, mCancel, &HfpAgent::busReady, this);
    return true;
}

void HfpAgent::busReady(GObject *, GAsyncResult *res, gpointer data)
{
    GError *error = nullptr;
    GDBusConnection *bus = g_bus_get_finish(res, &error);

    if (cancelled(error))
    {
        g_error_free(error);
        return;     /* stopped; data may be gone */
    }

    HfpAgent *self = static_cast<HfpAgent*>(data);
    if (!bus)
    {
        self->log(true, std::string("HFP agent: no system bus: ") +
                        (error ? error->message : "unknown error"));
        g_clear_error(&error);
        return;
    }

    self->attach(bus);
    g_object_unref(bus);
}

void HfpAgent::attach(GDBusConnection *bus)
{
    mBus = G_DBUS_CONNECTION(g_object_ref(bus));

    static const GDBusInterfaceVTable vtable = { &HfpAgent::methodCall, nullptr, nullptr, { nullptr } };
    GError *error = nullptr;
    mObject = g_dbus_connection_register_object(mBus, agentPath(), agentInfo()->interfaces[0],
                                                &vtable, this, nullptr, &error);
    if (!mObject)
    {
        log(true, std::string("HFP agent: cannot export ") + agentPath() + ": " +
                  (error ? error->message : "unknown error"));
        g_clear_error(&error);
        return;
    }

    /* Before GetCards, so a card that comes in between is not missed; one
     * reported twice is harmless. */
    mAddedSub = g_dbus_connection_signal_subscribe(mBus, OFONO_SERVICE, MANAGER_IFACE, "CardAdded",
                                                   "/", nullptr, G_DBUS_SIGNAL_FLAGS_NONE,
                                                   &HfpAgent::cardSignal, this, nullptr);
    mRemovedSub = g_dbus_connection_signal_subscribe(mBus, OFONO_SERVICE, MANAGER_IFACE, "CardRemoved",
                                                     "/", nullptr, G_DBUS_SIGNAL_FLAGS_NONE,
                                                     &HfpAgent::cardSignal, this, nullptr);

    mWatch = g_bus_watch_name_on_connection(mBus, OFONO_SERVICE, G_BUS_NAME_WATCHER_FLAGS_NONE,
                                            &HfpAgent::nameAppeared, &HfpAgent::nameVanished,
                                            this, nullptr);
}

void HfpAgent::stop()
{
    if (mCancel)
    {
        g_cancellable_cancel(mCancel);
        g_clear_object(&mCancel);
    }

    releaseSco();
    mPendingCard.clear();

    if (mBus)
    {
        if (mRegistered)
        {
            /* Fire and forget: oFono drops the agent anyway when audiod's
             * connection goes. */
            g_dbus_connection_call(mBus, OFONO_SERVICE, "/", MANAGER_IFACE, "Unregister",
                                   g_variant_new("(o)", agentPath()), nullptr,
                                   G_DBUS_CALL_FLAGS_NONE, -1, nullptr, nullptr, nullptr);
        }
        if (mWatch)
            g_bus_unwatch_name(mWatch);
        if (mAddedSub)
            g_dbus_connection_signal_unsubscribe(mBus, mAddedSub);
        if (mRemovedSub)
            g_dbus_connection_signal_unsubscribe(mBus, mRemovedSub);
        if (mObject)
            g_dbus_connection_unregister_object(mBus, mObject);
        g_clear_object(&mBus);
    }
    mWatch = mAddedSub = mRemovedSub = mObject = 0;
    mRegistered = false;
    mOfonoPresent = false;
    mOfonoOwner.clear();

    if (mContext)
    {
        g_main_context_unref(mContext);
        mContext = nullptr;
    }
}

void HfpAgent::nameAppeared(GDBusConnection *, const gchar *, const gchar *owner, gpointer data)
{
    static_cast<HfpAgent*>(data)->ofonoAppeared(owner);
}

void HfpAgent::nameVanished(GDBusConnection *, const gchar *, gpointer data)
{
    static_cast<HfpAgent*>(data)->ofonoVanished();
}

void HfpAgent::ofonoAppeared(const char *owner)
{
    if (mOfonoPresent && mOfonoOwner == (owner ? owner : ""))
        return;
    if (mOfonoPresent)
        ofonoVanished();    /* a new oFono: the old one's cards are gone */

    mOfonoPresent = true;
    mOfonoOwner = owner ? owner : "";
    log(false, "HFP agent: oFono is up (" + mOfonoOwner + "), registering");

    const guchar codecs[] = { CODEC_CVSD };
    GVariant *codecList = g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE, codecs, sizeof(codecs), 1);
    g_dbus_connection_call(mBus, OFONO_SERVICE, "/", MANAGER_IFACE, "Register",
                           g_variant_new("(o@ay)", agentPath(), codecList), nullptr,
                           G_DBUS_CALL_FLAGS_NONE, -1, mCancel, &HfpAgent::registerDone, this);

    g_dbus_connection_call(mBus, OFONO_SERVICE, "/", MANAGER_IFACE, "GetCards", nullptr,
                           G_VARIANT_TYPE("(a(oa{sv}))"), G_DBUS_CALL_FLAGS_NONE, -1, mCancel,
                           &HfpAgent::getCardsDone, this);
}

void HfpAgent::ofonoVanished()
{
    if (!mOfonoPresent)
        return;

    log(false, "HFP agent: oFono went away");
    mOfonoPresent = false;
    mOfonoOwner.clear();
    mRegistered = false;
    mPendingCard.clear();
    releaseSco();
    mListener.hfpServiceGone();
}

void HfpAgent::registerDone(GObject *source, GAsyncResult *res, gpointer data)
{
    GError *error = nullptr;
    GVariant *reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), res, &error);

    if (cancelled(error))
    {
        g_error_free(error);
        return;
    }

    HfpAgent *self = static_cast<HfpAgent*>(data);
    if (reply)
    {
        g_variant_unref(reply);
        self->mRegistered = true;
        self->log(false, "HFP agent: registered with oFono (CVSD)");
        return;
    }

    gchar *remote = error ? g_dbus_error_get_remote_error(error) : nullptr;
    /* InUse: PulseAudio's oFono backend (module-bluetooth-discover
     * headset=ofono or auto) got there first, and it would carry the audio
     * over the socket itself instead of leaving it to the HAL. */
    if (remote && !strcmp(remote, "org.ofono.Error.InUse"))
        self->log(true, "HFP agent: oFono already has a hands-free audio agent; "
                        "is PulseAudio's bluetooth module on headset=ofono?");
    else
        self->log(true, std::string("HFP agent: Register failed: ") +
                        (error ? error->message : "unknown error"));
    g_free(remote);
    g_clear_error(&error);
}

void HfpAgent::getCardsDone(GObject *source, GAsyncResult *res, gpointer data)
{
    GError *error = nullptr;
    GVariant *reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), res, &error);

    if (cancelled(error))
    {
        g_error_free(error);
        return;
    }

    HfpAgent *self = static_cast<HfpAgent*>(data);
    if (!reply)
    {
        self->log(true, std::string("HFP agent: GetCards failed: ") +
                        (error ? error->message : "unknown error"));
        g_clear_error(&error);
        return;
    }

    GVariantIter *cards = nullptr;
    const gchar *path = nullptr;
    GVariant *properties = nullptr;
    g_variant_get(reply, "(a(oa{sv}))", &cards);
    while (g_variant_iter_loop(cards, "(&o@a{sv})", &path, &properties))
        self->addCard(path, properties);
    g_variant_iter_free(cards);
    g_variant_unref(reply);
}

void HfpAgent::addCard(const char *path, GVariant *properties)
{
    std::string remote;
    if (!path || !parseCard(properties, &remote))
        return;
    mListener.hfpCardAdded(path, remote);
}

void HfpAgent::cardSignal(GDBusConnection *, const gchar *, const gchar *, const gchar *,
                          const gchar *signal, GVariant *params, gpointer data)
{
    HfpAgent *self = static_cast<HfpAgent*>(data);

    if (!g_strcmp0(signal, "CardAdded") && g_variant_is_of_type(params, G_VARIANT_TYPE("(oa{sv})")))
    {
        const gchar *path = nullptr;
        GVariant *properties = nullptr;
        g_variant_get(params, "(&o@a{sv})", &path, &properties);
        self->addCard(path, properties);
        g_variant_unref(properties);
    }
    else if (!g_strcmp0(signal, "CardRemoved") && g_variant_is_of_type(params, G_VARIANT_TYPE("(o)")))
    {
        const gchar *path = nullptr;
        g_variant_get(params, "(&o)", &path);
        if (self->mPendingCard == path)
            self->mPendingCard.clear();
        if (self->mScoCard == path)
            self->releaseSco();    /* the link went with the headset */
        self->mListener.hfpCardRemoved(path);
    }
}

bool HfpAgent::connectSco(const std::string &card)
{
    if (!mBus || !mOfonoPresent || card.empty() || !g_variant_is_object_path(card.c_str()))
        return false;
    if (!mPendingCard.empty())
        return mPendingCard == card;    /* one at a time; oFono answers Busy otherwise */

    mPendingCard = card;
    log(false, "HFP agent: asking oFono for an SCO link to " + card);
    g_dbus_connection_call(mBus, OFONO_SERVICE, card.c_str(), CARD_IFACE, "Connect", nullptr,
                           nullptr, G_DBUS_CALL_FLAGS_NONE, mConnectTimeoutMs, mCancel,
                           &HfpAgent::connectDone, this);
    return true;
}

void HfpAgent::connectDone(GObject *source, GAsyncResult *res, gpointer data)
{
    GError *error = nullptr;
    GVariant *reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), res, &error);

    if (cancelled(error))
    {
        g_error_free(error);
        return;
    }

    HfpAgent *self = static_cast<HfpAgent*>(data);
    const std::string card = self->mPendingCard;
    self->mPendingCard.clear();

    if (reply)
    {
        g_variant_unref(reply);
        /* oFono sends NewConnection before it answers Connect, so the
         * socket is normally here already. If not, it never will be. */
        if (!card.empty() && self->mScoCard != card)
            self->mListener.hfpScoConnectFailed(card, "oFono answered without handing over a link");
        return;
    }

    const std::string message = error ? error->message : "unknown error";
    g_clear_error(&error);
    if (card.empty())
        return;     /* the card went in the meantime */
    if (self->mScoCard == card)
        return;     /* it came anyway */
    self->log(true, "HFP agent: SCO link to " + card + " failed: " + message);
    self->mListener.hfpScoConnectFailed(card, message);
}

void HfpAgent::methodCall(GDBusConnection *, const gchar *sender, const gchar *, const gchar *,
                          const gchar *method, GVariant *params, GDBusMethodInvocation *inv,
                          gpointer data)
{
    HfpAgent *self = static_cast<HfpAgent*>(data);

    /* The policy lets root processes call any agent; only oFono's calls
     * are oFono's. */
    if (!self->mOfonoPresent || g_strcmp0(sender, self->mOfonoOwner.c_str()) != 0)
    {
        g_dbus_method_invocation_return_dbus_error(inv, "org.ofono.Error.NotAllowed",
                                                   "Only oFono talks to this agent");
        return;
    }

    if (!g_strcmp0(method, "Release"))
    {
        self->log(false, "HFP agent: released by oFono");
        self->mRegistered = false;
        g_dbus_method_invocation_return_value(inv, nullptr);
        return;
    }

    if (g_strcmp0(method, "NewConnection") != 0)
    {
        g_dbus_method_invocation_return_dbus_error(inv, "org.freedesktop.DBus.Error.UnknownMethod",
                                                   "Unknown method");
        return;
    }

    const gchar *card = nullptr;
    gint32 index = -1;
    guchar codec = 0;
    g_variant_get(params, "(&ohy)", &card, &index, &codec);

    GUnixFDList *fds = g_dbus_message_get_unix_fd_list(g_dbus_method_invocation_get_message(inv));
    GError *error = nullptr;
    const int fd = fds ? g_unix_fd_list_get(fds, index, &error) : -1;
    if (fd < 0)
    {
        self->log(true, std::string("HFP agent: NewConnection without a usable socket: ") +
                        (error ? error->message : "no file descriptors"));
        g_clear_error(&error);
        g_dbus_method_invocation_return_dbus_error(inv, "org.ofono.Error.InvalidArguments",
                                                   "No SCO socket");
        return;
    }

    g_dbus_method_invocation_return_value(inv, nullptr);
    if (self->mPendingCard == card)
        self->mPendingCard.clear();
    self->holdSco(card, fd, codec);
}

void HfpAgent::holdSco(const std::string &card, int fd, unsigned codec)
{
    /* One link at a time: a new one (another headset, or the same one again)
     * replaces whatever was held. */
    releaseSco();

    mScoFd = fd;
    mScoCard = card;
    mScoWatch = g_unix_fd_source_new(fd, static_cast<GIOCondition>(G_IO_HUP | G_IO_ERR));
    g_source_set_callback(mScoWatch, G_SOURCE_FUNC(&HfpAgent::scoEvent), this, nullptr);
    g_source_attach(mScoWatch, mContext);

    log(false, "HFP agent: holding the SCO link to " + card + " (codec " +
               std::to_string(codec) + ")");
    mListener.hfpScoConnected(card, codec);
}

gboolean HfpAgent::scoEvent(gint, GIOCondition, gpointer data)
{
    HfpAgent *self = static_cast<HfpAgent*>(data);
    const std::string card = self->mScoCard;

    self->log(false, "HFP agent: the SCO link to " + card + " closed");
    /* The source is being dispatched: drop it by returning REMOVE, not by
     * destroying it underneath GLib. */
    g_source_unref(self->mScoWatch);
    self->mScoWatch = nullptr;
    close(self->mScoFd);
    self->mScoFd = -1;
    self->mScoCard.clear();

    self->mListener.hfpScoClosed(card);
    return G_SOURCE_REMOVE;
}

void HfpAgent::releaseSco()
{
    if (mScoWatch)
    {
        g_source_destroy(mScoWatch);
        g_source_unref(mScoWatch);
        mScoWatch = nullptr;
    }
    if (mScoFd >= 0)
    {
        /* Closing the socket is what takes the link down. */
        close(mScoFd);
        mScoFd = -1;
    }
    mScoCard.clear();
}

void HfpAgent::disconnectSco()
{
    if (mScoFd >= 0)
        log(false, "HFP agent: closing the SCO link to " + mScoCard);
    releaseSco();
}
