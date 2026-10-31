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

#ifndef _HFP_AGENT_H
#define _HFP_AGENT_H

#include <functional>
#include <string>

#include <gio/gio.h>

/*
 * oFono's hands-free audio agent (org.ofono.HandsfreeAudioAgent), for the
 * phone side of HFP: oFono's hfp_ag_bluez5 is the audio gateway, and this is
 * the one process it hands SCO links to.
 *
 * It follows org.ofono on the system bus; while oFono is there it is
 * registered with HandsfreeAudioManager, reports every headset card (Type
 * "gateway": the cards hfp_ag_bluez5 makes) and holds the socket of at most
 * one SCO link. It does not read or write that socket: on a Halium phone the
 * Android audio HAL carries the call audio, and the socket only keeps the link
 * up. Only CVSD is offered, so oFono never negotiates mSBC, which would need
 * the HAL told bt_wbs=on and a kernel with transparent SCO.
 *
 * Everything runs on the GLib main context that was the thread default when
 * start() was called (audiod's main loop). Listener calls come from there.
 */
class HfpAgent
{
public:
    class Listener
    {
    public:
        virtual ~Listener() = default;
        //! A headset is connected to the gateway (its card appeared).
        virtual void hfpCardAdded(const std::string &card, const std::string &remote) = 0;
        virtual void hfpCardRemoved(const std::string &card) = 0;
        //! oFono handed over an SCO link to card; the agent holds it now.
        virtual void hfpScoConnected(const std::string &card, unsigned codec) = 0;
        //! The link asked for with connectSco() could not be opened.
        virtual void hfpScoConnectFailed(const std::string &card, const std::string &error) = 0;
        //! The link held closed by itself (the headset or the radio), not by
        //! disconnectSco().
        virtual void hfpScoClosed(const std::string &card) = 0;
        //! oFono went away: every card is gone and no link is held.
        virtual void hfpServiceGone() = 0;
    };

    typedef std::function<void(bool error, const std::string &message)> Log;

    HfpAgent(Listener &listener, Log log);
    ~HfpAgent();
    HfpAgent(const HfpAgent &) = delete;
    HfpAgent &operator=(const HfpAgent &) = delete;

    //! Follow oFono on bus, or on the system bus when bus is null (connected
    //! asynchronously then). False if that could not even be started.
    bool start(GDBusConnection *bus = nullptr);
    //! Unregister, drop the link and stop following oFono.
    void stop();

    //! Ask oFono for an SCO link to card. The answer comes through the
    //! listener: hfpScoConnected() or hfpScoConnectFailed().
    bool connectSco(const std::string &card);
    //! Close the link held, if any.
    void disconnectSco();

    bool registered() const { return mRegistered; }
    bool holdingSco() const { return mScoFd >= 0; }
    const std::string &scoCard() const { return mScoCard; }

    //! How long to wait for oFono to open a link, in ms. oFono answers Connect
    //! only once the link is up or has failed, and gives up on a link that
    //! never completes without answering at all.
    void setConnectTimeout(int ms) { mConnectTimeoutMs = ms; }

    static const char *agentPath() { return "/org/webosports/audiod/hfp"; }

    //! The parts of a CardAdded/GetCards entry this needs: whether it is a
    //! headset card and its address. Public for the tests.
    static bool parseCard(GVariant *properties, std::string *remote);

private:
    Listener &mListener;
    Log mLog;
    GDBusConnection *mBus = nullptr;
    GCancellable *mCancel = nullptr;
    GMainContext *mContext = nullptr;
    guint mWatch = 0;
    guint mObject = 0;
    guint mAddedSub = 0;
    guint mRemovedSub = 0;
    bool mRegistered = false;
    bool mOfonoPresent = false;
    std::string mOfonoOwner;

    int mScoFd = -1;
    std::string mScoCard;
    GSource *mScoWatch = nullptr;
    std::string mPendingCard;
    int mConnectTimeoutMs = 15000;

    void log(bool error, const std::string &message);
    void attach(GDBusConnection *bus);
    void ofonoAppeared(const char *owner);
    void ofonoVanished();
    void addCard(const char *path, GVariant *properties);
    void releaseSco();
    void holdSco(const std::string &card, int fd, unsigned codec);

    static void busReady(GObject *source, GAsyncResult *res, gpointer data);
    static void nameAppeared(GDBusConnection *c, const gchar *name, const gchar *owner, gpointer data);
    static void nameVanished(GDBusConnection *c, const gchar *name, gpointer data);
    static void registerDone(GObject *source, GAsyncResult *res, gpointer data);
    static void getCardsDone(GObject *source, GAsyncResult *res, gpointer data);
    static void connectDone(GObject *source, GAsyncResult *res, gpointer data);
    static void cardSignal(GDBusConnection *c, const gchar *sender, const gchar *path,
                           const gchar *iface, const gchar *signal, GVariant *params,
                           gpointer data);
    static void methodCall(GDBusConnection *c, const gchar *sender, const gchar *path,
                           const gchar *iface, const gchar *method, GVariant *params,
                           GDBusMethodInvocation *inv, gpointer data);
    static gboolean scoEvent(gint fd, GIOCondition cond, gpointer data);
};

#endif // _HFP_AGENT_H
