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

// HfpAgent against a stand-in for oFono's HandsfreeAudioManager and
// HandsfreeAudioCard on a private bus (a dbus-daemon of the test's own), with
// socketpair halves standing in for SCO sockets.

#include <gtest/gtest.h>

#include <algorithm>
#include <functional>
#include <map>
#include <cstdlib>
#include <poll.h>
#include <signal.h>
#include <string>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

#include <gio/gio.h>
#include <gio/gunixfdlist.h>

#include "hfpAgent.h"

namespace {

const char OFONO_XML[] =
    "<node>"
    "  <interface name='org.ofono.HandsfreeAudioManager'>"
    "    <method name='GetCards'><arg type='a(oa{sv})' direction='out'/></method>"
    "    <method name='Register'><arg type='o' direction='in'/><arg type='ay' direction='in'/></method>"
    "    <method name='Unregister'><arg type='o' direction='in'/></method>"
    "    <signal name='CardAdded'><arg type='o'/><arg type='a{sv}'/></signal>"
    "    <signal name='CardRemoved'><arg type='o'/></signal>"
    "  </interface>"
    "  <interface name='org.ofono.HandsfreeAudioCard'>"
    "    <method name='Connect'/>"
    "  </interface>"
    "</node>";

// A dbus-daemon of the test's own. Not GTestDBus: that runs the daemon with
// the test's environment, and run through the Yocto sysroot's loader that
// LD_LIBRARY_PATH would load the sysroot's libraries into the host's
// dbus-daemon. HFP_TEST_DBUS_DAEMON names another daemon binary.
class PrivateBus
{
public:
    PrivateBus()
    {
        const char *daemon = getenv("HFP_TEST_DBUS_DAEMON");
        gchar *argv[] = { const_cast<gchar*>(daemon ? daemon : "/usr/bin/dbus-daemon"),
                          const_cast<gchar*>("--session"), const_cast<gchar*>("--nofork"),
                          const_cast<gchar*>("--print-address=1"), nullptr };
        gchar *envp[] = { const_cast<gchar*>("PATH=/usr/bin:/bin"), nullptr };
        gint out = -1;
        GError *error = nullptr;
        if (!g_spawn_async_with_pipes(nullptr, argv, envp, G_SPAWN_DO_NOT_REAP_CHILD, nullptr, nullptr,
                                      &mPid, nullptr, &out, nullptr, &error))
        {
            ADD_FAILURE() << "dbus-daemon: " << (error ? error->message : "?");
            g_clear_error(&error);
            return;
        }
        char buf[512];
        size_t got = 0;
        while (got < sizeof(buf) - 1)
        {
            const ssize_t n = read(out, buf + got, 1);
            if (n <= 0 || buf[got] == '\n')
                break;
            got++;
        }
        buf[got] = '\0';
        close(out);
        mAddress = buf;
    }

    ~PrivateBus()
    {
        if (mPid > 0)
        {
            kill(mPid, SIGTERM);
            waitpid(mPid, nullptr, 0);
            g_spawn_close_pid(mPid);
        }
    }

    const std::string &address() const { return mAddress; }

private:
    GPid mPid = 0;
    std::string mAddress;
};

// Runs the default main context until done() or about two seconds pass.
bool spinUntil(const std::function<bool()> &done, int ms = 2000)
{
    const gint64 end = g_get_monotonic_time() + ms * 1000;
    while (!done())
    {
        if (g_get_monotonic_time() > end)
            return false;
        g_main_context_iteration(nullptr, FALSE);
        g_usleep(1000);
    }
    return true;
}

void spinFor(int ms)
{
    spinUntil([] { return false; }, ms);
}

GDBusConnection *connectTo(const char *address)
{
    GError *error = nullptr;
    GDBusConnection *c = g_dbus_connection_new_for_address_sync(
        address,
        static_cast<GDBusConnectionFlags>(G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
                                          G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION),
        nullptr, nullptr, &error);
    if (error)
    {
        ADD_FAILURE() << error->message;
        g_error_free(error);
    }
    return c;
}

GVariant *cardProperties(const char *type, const char *remote)
{
    GVariantBuilder b;
    g_variant_builder_init(&b, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&b, "{sv}", "Type", g_variant_new_string(type));
    g_variant_builder_add(&b, "{sv}", "RemoteAddress", g_variant_new_string(remote));
    g_variant_builder_add(&b, "{sv}", "LocalAddress", g_variant_new_string("AA:BB:CC:DD:EE:FF"));
    return g_variant_builder_end(&b);
}

// What oFono does on the bus, as much of it as HfpAgent uses.
class FakeOfono
{
public:
    enum class OnConnect { HandOver, Fail, Hang, ReplyOnly };

    explicit FakeOfono(const char *address) : mBus(connectTo(address))
    {
        mInfo = g_dbus_node_info_new_for_xml(OFONO_XML, nullptr);
        static const GDBusInterfaceVTable vtable = { &FakeOfono::methodCall, nullptr, nullptr, { nullptr } };
        mManagerId = g_dbus_connection_register_object(mBus, "/", mInfo->interfaces[0], &vtable,
                                                       this, nullptr, nullptr);
    }

    ~FakeOfono()
    {
        for (int fd : mPeers)
            close(fd);
        for (auto &c : mCardIds)
            g_dbus_connection_unregister_object(mBus, c.second);
        g_dbus_connection_unregister_object(mBus, mManagerId);
        // Answered at last: an invocation never returned keeps its message
        // and connection alive.
        for (GDBusMethodInvocation *inv : mHeld)
            g_dbus_method_invocation_return_dbus_error(inv, "org.ofono.Error.Failed", "Gone");
        g_dbus_node_info_unref(mInfo);
        g_object_unref(mBus);
    }

    void own()
    {
        GVariant *r = g_dbus_connection_call_sync(mBus, "org.freedesktop.DBus", "/org/freedesktop/DBus",
                                                  "org.freedesktop.DBus", "RequestName",
                                                  g_variant_new("(su)", "org.ofono", 0u), nullptr,
                                                  G_DBUS_CALL_FLAGS_NONE, -1, nullptr, nullptr);
        ASSERT_NE(r, nullptr);
        g_variant_unref(r);
    }

    void disown()
    {
        GVariant *r = g_dbus_connection_call_sync(mBus, "org.freedesktop.DBus", "/org/freedesktop/DBus",
                                                  "org.freedesktop.DBus", "ReleaseName",
                                                  g_variant_new("(s)", "org.ofono"), nullptr,
                                                  G_DBUS_CALL_FLAGS_NONE, -1, nullptr, nullptr);
        ASSERT_NE(r, nullptr);
        g_variant_unref(r);
    }

    void addCard(const std::string &path, const char *type, const char *remote, bool signal)
    {
        static const GDBusInterfaceVTable vtable = { &FakeOfono::methodCall, nullptr, nullptr, { nullptr } };
        mCards[path] = std::make_pair(std::string(type), std::string(remote));
        mCardIds[path] = g_dbus_connection_register_object(mBus, path.c_str(), mInfo->interfaces[1],
                                                           &vtable, this, nullptr, nullptr);
        if (signal)
            g_dbus_connection_emit_signal(mBus, nullptr, "/", "org.ofono.HandsfreeAudioManager",
                                          "CardAdded",
                                          g_variant_new("(o@a{sv})", path.c_str(),
                                                        cardProperties(type, remote)),
                                          nullptr);
    }

    void removeCard(const std::string &path)
    {
        mCards.erase(path);
        g_dbus_connection_unregister_object(mBus, mCardIds[path]);
        mCardIds.erase(path);
        g_dbus_connection_emit_signal(mBus, nullptr, "/", "org.ofono.HandsfreeAudioManager",
                                      "CardRemoved", g_variant_new("(o)", path.c_str()), nullptr);
    }

    // Sends NewConnection with a fresh socketpair half, as sco_accept() and
    // sco_connect_cb() do: no reply expected. Returns the half kept here.
    int newConnection(GDBusConnection *from, const std::string &card, guchar codec = 1)
    {
        int sv[2];
        if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, sv) < 0)
            return -1;

        GDBusMessage *msg = g_dbus_message_new_method_call(mAgentOwner.c_str(), mAgentPath.c_str(),
                                                           "org.ofono.HandsfreeAudioAgent",
                                                           "NewConnection");
        GUnixFDList *fds = g_unix_fd_list_new();
        const gint fdIndex = g_unix_fd_list_append(fds, sv[1], nullptr);
        close(sv[1]);   // the list holds a copy, as oFono close()s after sending
        g_dbus_message_set_unix_fd_list(msg, fds);
        g_dbus_message_set_body(msg, g_variant_new("(ohy)", card.c_str(), fdIndex, codec));
        g_dbus_message_set_flags(msg, G_DBUS_MESSAGE_FLAGS_NO_REPLY_EXPECTED);
        g_dbus_connection_send_message(from ? from : mBus, msg, G_DBUS_SEND_MESSAGE_FLAGS_NONE,
                                       nullptr, nullptr);
        g_object_unref(fds);
        g_object_unref(msg);
        mPeers.push_back(sv[0]);
        return sv[0];
    }

    GDBusConnection *mBus;
    GDBusNodeInfo *mInfo = nullptr;
    guint mManagerId = 0;
    std::map<std::string, guint> mCardIds;
    std::map<std::string, std::pair<std::string, std::string>> mCards;

    std::string mAgentOwner;
    std::string mAgentPath;
    std::vector<guchar> mCodecs;
    int mRegisterCalls = 0;
    int mUnregisterCalls = 0;
    bool mRefuseRegister = false;
    OnConnect mOnConnect = OnConnect::HandOver;
    std::vector<std::string> mConnects;
    std::vector<int> mPeers;
    std::vector<GDBusMethodInvocation*> mHeld;

private:
    static void methodCall(GDBusConnection *, const gchar *sender, const gchar *path,
                           const gchar *, const gchar *method, GVariant *params,
                           GDBusMethodInvocation *inv, gpointer data)
    {
        FakeOfono *self = static_cast<FakeOfono*>(data);

        if (!g_strcmp0(method, "Register"))
        {
            self->mRegisterCalls++;
            if (self->mRefuseRegister)
            {
                g_dbus_method_invocation_return_dbus_error(inv, "org.ofono.Error.InUse", "Operation already in use");
                return;
            }
            const gchar *agent = nullptr;
            GVariant *codecs = nullptr;
            g_variant_get(params, "(&o@ay)", &agent, &codecs);
            gsize n = 0;
            const guchar *c = static_cast<const guchar*>(g_variant_get_fixed_array(codecs, &n, 1));
            self->mCodecs.assign(c, c + n);
            g_variant_unref(codecs);
            self->mAgentOwner = sender;
            self->mAgentPath = agent;
            g_dbus_method_invocation_return_value(inv, nullptr);
        }
        else if (!g_strcmp0(method, "Unregister"))
        {
            self->mUnregisterCalls++;
            g_dbus_method_invocation_return_value(inv, nullptr);
        }
        else if (!g_strcmp0(method, "GetCards"))
        {
            GVariantBuilder b;
            g_variant_builder_init(&b, G_VARIANT_TYPE("a(oa{sv})"));
            for (auto &c : self->mCards)
                g_variant_builder_add(&b, "(o@a{sv})", c.first.c_str(),
                                      cardProperties(c.second.first.c_str(), c.second.second.c_str()));
            g_dbus_method_invocation_return_value(inv, g_variant_new("(a(oa{sv}))", &b));
        }
        else if (!g_strcmp0(method, "Connect"))
        {
            self->mConnects.push_back(path);
            switch (self->mOnConnect)
            {
                case OnConnect::HandOver:
                    // sco_connect_cb(): NewConnection first, then the reply.
                    self->newConnection(nullptr, path);
                    g_dbus_method_invocation_return_value(inv, nullptr);
                    break;
                case OnConnect::ReplyOnly:
                    g_dbus_method_invocation_return_value(inv, nullptr);
                    break;
                case OnConnect::Fail:
                    g_dbus_method_invocation_return_dbus_error(inv, "org.ofono.Error.Failed", "Operation failed");
                    break;
                case OnConnect::Hang:
                    // The handler owns inv until it answers: kept, unanswered.
                    self->mHeld.push_back(inv);
                    break;
            }
        }
    }
};

struct Recorder : public HfpAgent::Listener
{
    std::vector<std::string> events;
    std::vector<std::string> errors;

    void hfpCardAdded(const std::string &card, const std::string &remote) override
    { events.push_back("added " + card + " " + remote); }
    void hfpCardRemoved(const std::string &card) override
    { events.push_back("removed " + card); }
    void hfpScoConnected(const std::string &card, unsigned codec) override
    { events.push_back("sco " + card + " " + std::to_string(codec)); }
    void hfpScoConnectFailed(const std::string &card, const std::string &) override
    { events.push_back("failed " + card); }
    void hfpScoClosed(const std::string &card) override
    { events.push_back("closed " + card); }
    void hfpServiceGone() override
    { events.push_back("gone"); }

    bool has(const std::string &e) const
    {
        return std::find(events.begin(), events.end(), e) != events.end();
    }
};

// The peer of a socket the agent closed sees end of file.
bool peerClosed(int fd)
{
    struct pollfd p = { fd, POLLIN, 0 };
    if (poll(&p, 1, 0) <= 0)
        return false;
    char c;
    return (p.revents & POLLHUP) || recv(fd, &c, 1, MSG_DONTWAIT) == 0;
}

class HfpAgentTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        mDbus = new PrivateBus();
        mAddress = mDbus->address();
        ASSERT_FALSE(mAddress.empty());
        mOfono = new FakeOfono(mAddress.c_str());
        mAgentBus = connectTo(mAddress.c_str());
        mAgent = new HfpAgent(mRec, [this](bool error, const std::string &m) {
            if (error)
                mRec.errors.push_back(m);
        });
    }

    void TearDown() override
    {
        delete mAgent;
        spinFor(20);    // anything still queued must not reach a deleted agent
        g_object_unref(mAgentBus);
        delete mOfono;
        spinFor(50);    // let GDBus deliver what is still queued and drop its references
        delete mDbus;
    }

    void startRegistered()
    {
        mOfono->own();
        ASSERT_TRUE(mAgent->start(mAgentBus));
        ASSERT_TRUE(spinUntil([this] { return mAgent->registered(); }));
    }

    PrivateBus *mDbus = nullptr;
    std::string mAddress;
    FakeOfono *mOfono = nullptr;
    GDBusConnection *mAgentBus = nullptr;
    Recorder mRec;
    HfpAgent *mAgent = nullptr;
};

TEST(HfpAgentParse, OnlyGatewayCardsAreHeadsets)
{
    std::string remote;
    GVariant *gw = g_variant_ref_sink(cardProperties("gateway", "00:11:22:33:44:55"));
    GVariant *hf = g_variant_ref_sink(cardProperties("handsfree", "00:11:22:33:44:66"));
    GVariant *wrong = g_variant_ref_sink(g_variant_new_string("gateway"));
    GVariantBuilder b;
    g_variant_builder_init(&b, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&b, "{sv}", "Type", g_variant_new_string("gateway"));
    GVariant *noAddress = g_variant_ref_sink(g_variant_builder_end(&b));

    EXPECT_TRUE(HfpAgent::parseCard(gw, &remote));
    EXPECT_EQ(remote, "00:11:22:33:44:55");
    EXPECT_FALSE(HfpAgent::parseCard(hf, &remote));
    EXPECT_FALSE(HfpAgent::parseCard(wrong, &remote));
    EXPECT_FALSE(HfpAgent::parseCard(nullptr, &remote));
    EXPECT_TRUE(HfpAgent::parseCard(noAddress, &remote));
    EXPECT_EQ(remote, "");
    EXPECT_TRUE(HfpAgent::parseCard(gw, nullptr));

    g_variant_unref(gw);
    g_variant_unref(hf);
    g_variant_unref(wrong);
    g_variant_unref(noAddress);
}

TEST_F(HfpAgentTest, RegistersOfferingOnlyCvsdAndReportsTheHeadsetsThere)
{
    mOfono->addCard("/card_1", "gateway", "00:11:22:33:44:55", false);
    mOfono->addCard("/card_2", "handsfree", "00:11:22:33:44:66", false);
    startRegistered();

    EXPECT_EQ(mOfono->mCodecs, std::vector<guchar>{1});
    EXPECT_EQ(mOfono->mAgentPath, HfpAgent::agentPath());
    ASSERT_TRUE(spinUntil([this] { return !mRec.events.empty(); }));
    spinFor(50);
    EXPECT_EQ(mRec.events, std::vector<std::string>{"added /card_1 00:11:22:33:44:55"});
}

TEST_F(HfpAgentTest, WaitsForOfono)
{
    ASSERT_TRUE(mAgent->start(mAgentBus));
    spinFor(100);
    EXPECT_FALSE(mAgent->registered());
    EXPECT_EQ(mOfono->mRegisterCalls, 0);
    mOfono->own();
    EXPECT_TRUE(spinUntil([this] { return mAgent->registered(); }));
}

TEST_F(HfpAgentTest, FollowsCardsComingAndGoing)
{
    startRegistered();
    mOfono->addCard("/card_3", "gateway", "11:22:33:44:55:66", true);
    mOfono->addCard("/card_4", "handsfree", "11:22:33:44:55:77", true);
    ASSERT_TRUE(spinUntil([this] { return mRec.has("added /card_3 11:22:33:44:55:66"); }));
    mOfono->removeCard("/card_3");
    ASSERT_TRUE(spinUntil([this] { return mRec.has("removed /card_3"); }));
    EXPECT_FALSE(mRec.has("added /card_4 11:22:33:44:55:77"));
}

TEST_F(HfpAgentTest, HoldsTheLinkOfonoHandsOver)
{
    mOfono->addCard("/card_1", "gateway", "A", false);
    startRegistered();

    ASSERT_TRUE(mAgent->connectSco("/card_1"));
    ASSERT_TRUE(spinUntil([this] { return mRec.has("sco /card_1 1"); }));
    EXPECT_TRUE(mAgent->holdingSco());
    EXPECT_EQ(mAgent->scoCard(), "/card_1");
    EXPECT_EQ(mOfono->mConnects, std::vector<std::string>{"/card_1"});
    spinFor(50);
    EXPECT_FALSE(mRec.has("failed /card_1"));       // the reply after the socket
    EXPECT_FALSE(peerClosed(mOfono->mPeers.back()));
}

TEST_F(HfpAgentTest, ClosingTheLinkClosesTheSocket)
{
    mOfono->addCard("/card_1", "gateway", "A", false);
    startRegistered();
    mAgent->connectSco("/card_1");
    ASSERT_TRUE(spinUntil([this] { return mAgent->holdingSco(); }));

    mAgent->disconnectSco();
    EXPECT_FALSE(mAgent->holdingSco());
    EXPECT_TRUE(peerClosed(mOfono->mPeers.back()));
    spinFor(50);
    EXPECT_FALSE(mRec.has("closed /card_1"));      // audiod's own doing
}

TEST_F(HfpAgentTest, TheHeadsetClosingTheLinkIsReported)
{
    mOfono->addCard("/card_1", "gateway", "A", false);
    startRegistered();
    mAgent->connectSco("/card_1");
    ASSERT_TRUE(spinUntil([this] { return mAgent->holdingSco(); }));

    close(mOfono->mPeers.back());
    mOfono->mPeers.pop_back();
    ASSERT_TRUE(spinUntil([this] { return mRec.has("closed /card_1"); }));
    EXPECT_FALSE(mAgent->holdingSco());
    EXPECT_EQ(mAgent->scoCard(), "");
}

TEST_F(HfpAgentTest, TheHeadsetGoingTakesItsLinkWithIt)
{
    mOfono->addCard("/card_1", "gateway", "A", false);
    mOfono->addCard("/card_2", "gateway", "B", false);
    startRegistered();
    mAgent->connectSco("/card_1");
    ASSERT_TRUE(spinUntil([this] { return mAgent->holdingSco(); }));

    mOfono->removeCard("/card_2");      // another headset: the link stays
    ASSERT_TRUE(spinUntil([this] { return mRec.has("removed /card_2"); }));
    EXPECT_TRUE(mAgent->holdingSco());

    mOfono->removeCard("/card_1");
    ASSERT_TRUE(spinUntil([this] { return mRec.has("removed /card_1"); }));
    EXPECT_FALSE(mAgent->holdingSco());
    EXPECT_TRUE(peerClosed(mOfono->mPeers.back()));
    EXPECT_FALSE(mRec.has("closed /card_1"));      // reported as the card going
}

TEST_F(HfpAgentTest, ALinkTheHeadsetOpensIsHeldToo)
{
    mOfono->addCard("/card_1", "gateway", "A", false);
    startRegistered();
    mOfono->newConnection(nullptr, "/card_1");
    ASSERT_TRUE(spinUntil([this] { return mRec.has("sco /card_1 1"); }));
    EXPECT_TRUE(mAgent->holdingSco());
}

TEST_F(HfpAgentTest, ANewLinkReplacesTheOldOne)
{
    mOfono->addCard("/card_1", "gateway", "A", false);
    mOfono->addCard("/card_2", "gateway", "B", false);
    startRegistered();
    const int first = mOfono->newConnection(nullptr, "/card_1");
    ASSERT_TRUE(spinUntil([this] { return mRec.has("sco /card_1 1"); }));
    mOfono->newConnection(nullptr, "/card_2");
    ASSERT_TRUE(spinUntil([this] { return mRec.has("sco /card_2 1"); }));
    EXPECT_EQ(mAgent->scoCard(), "/card_2");
    EXPECT_TRUE(peerClosed(first));
    EXPECT_FALSE(mRec.has("closed /card_1"));
}

TEST_F(HfpAgentTest, AFailedLinkIsReported)
{
    mOfono->addCard("/card_1", "gateway", "A", false);
    startRegistered();
    mOfono->mOnConnect = FakeOfono::OnConnect::Fail;
    ASSERT_TRUE(mAgent->connectSco("/card_1"));
    ASSERT_TRUE(spinUntil([this] { return mRec.has("failed /card_1"); }));
    EXPECT_FALSE(mAgent->holdingSco());
    // And the next one may be asked for.
    mOfono->mOnConnect = FakeOfono::OnConnect::HandOver;
    ASSERT_TRUE(mAgent->connectSco("/card_1"));
    EXPECT_TRUE(spinUntil([this] { return mRec.has("sco /card_1 1"); }));
}

TEST_F(HfpAgentTest, ALinkThatNeverComesTimesOut)
{
    mOfono->addCard("/card_1", "gateway", "A", false);
    startRegistered();
    mOfono->mOnConnect = FakeOfono::OnConnect::Hang;
    mAgent->setConnectTimeout(200);
    ASSERT_TRUE(mAgent->connectSco("/card_1"));
    EXPECT_TRUE(mAgent->connectSco("/card_1"));     // the same one is in flight
    EXPECT_FALSE(mAgent->connectSco("/card_2"));    // and only one at a time
    EXPECT_TRUE(spinUntil([this] { return mRec.has("failed /card_1"); }));
    EXPECT_EQ(mOfono->mConnects.size(), 1u);
}

TEST_F(HfpAgentTest, AnAnswerWithoutALinkIsAFailure)
{
    mOfono->addCard("/card_1", "gateway", "A", false);
    startRegistered();
    mOfono->mOnConnect = FakeOfono::OnConnect::ReplyOnly;
    mAgent->connectSco("/card_1");
    EXPECT_TRUE(spinUntil([this] { return mRec.has("failed /card_1"); }));
}

TEST_F(HfpAgentTest, NobodyButOfonoHandsOverALink)
{
    mOfono->addCard("/card_1", "gateway", "A", false);
    startRegistered();
    GDBusConnection *stranger = connectTo(mAddress.c_str());
    mOfono->newConnection(stranger, "/card_1");
    spinFor(100);
    EXPECT_FALSE(mAgent->holdingSco());
    EXPECT_FALSE(mRec.has("sco /card_1 1"));
    g_object_unref(stranger);
}

TEST_F(HfpAgentTest, NotConnectingWithoutOfonoOrToABadPath)
{
    ASSERT_TRUE(mAgent->start(mAgentBus));
    EXPECT_FALSE(mAgent->connectSco("/card_1"));
    mOfono->own();
    ASSERT_TRUE(spinUntil([this] { return mAgent->registered(); }));
    EXPECT_FALSE(mAgent->connectSco(""));
    EXPECT_FALSE(mAgent->connectSco("not a path"));
}

TEST_F(HfpAgentTest, OfonoGoingDropsEverythingAndComingBackRegistersAgain)
{
    mOfono->addCard("/card_1", "gateway", "A", false);
    startRegistered();
    mAgent->connectSco("/card_1");
    ASSERT_TRUE(spinUntil([this] { return mAgent->holdingSco(); }));

    mOfono->disown();
    ASSERT_TRUE(spinUntil([this] { return mRec.has("gone"); }));
    EXPECT_FALSE(mAgent->holdingSco());
    EXPECT_FALSE(mAgent->registered());
    EXPECT_TRUE(peerClosed(mOfono->mPeers.back()));
    EXPECT_FALSE(mRec.has("closed /card_1"));

    mOfono->own();
    EXPECT_TRUE(spinUntil([this] { return mAgent->registered(); }));
    EXPECT_EQ(mOfono->mRegisterCalls, 2);
}

TEST_F(HfpAgentTest, AnotherAgentAlreadyThereIsReported)
{
    mOfono->mRefuseRegister = true;
    mOfono->own();
    ASSERT_TRUE(mAgent->start(mAgentBus));
    ASSERT_TRUE(spinUntil([this] { return !mRec.errors.empty(); }));
    EXPECT_FALSE(mAgent->registered());
    EXPECT_NE(mRec.errors.back().find("already has a hands-free audio agent"), std::string::npos);
}

TEST_F(HfpAgentTest, StoppingUnregistersAndLetsGoOfTheLink)
{
    mOfono->addCard("/card_1", "gateway", "A", false);
    startRegistered();
    mAgent->connectSco("/card_1");
    ASSERT_TRUE(spinUntil([this] { return mAgent->holdingSco(); }));
    mAgent->stop();
    EXPECT_FALSE(mAgent->holdingSco());
    EXPECT_TRUE(peerClosed(mOfono->mPeers.back()));
    EXPECT_TRUE(spinUntil([this] { return mOfono->mUnregisterCalls == 1; }));
}

TEST_F(HfpAgentTest, StoppingWithACallInFlightCallsNothingBack)
{
    mOfono->addCard("/card_1", "gateway", "A", false);
    startRegistered();
    mOfono->mOnConnect = FakeOfono::OnConnect::Hang;
    mAgent->setConnectTimeout(100);
    mAgent->connectSco("/card_1");
    spinUntil([this] { return !mOfono->mHeld.empty(); });
    const size_t before = mRec.events.size();
    delete mAgent;
    mAgent = nullptr;
    spinFor(300);   // past the timeout: the cancelled call must not land
    EXPECT_EQ(mRec.events.size(), before);
}

} // namespace
