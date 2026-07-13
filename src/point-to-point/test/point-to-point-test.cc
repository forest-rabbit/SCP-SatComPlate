/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2009 INRIA
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Author: Mathieu Lacage <mathieu.lacage@sophia.inria.fr>
 */

#include "ns3/test.h"
#include "ns3/drop-tail-queue.h"
#include "ns3/simulator.h"
#include "ns3/point-to-point-net-device.h"
#include "ns3/point-to-point-channel.h"
#include "ns3/net-device-queue-interface.h"

using namespace ns3;

/**
 * \brief Test class for PointToPoint model
 *
 * It tries to send one packet from one NetDevice to another, over a
 * PointToPointChannel.
 */
class PointToPointTest : public TestCase
{
public:
  /**
   * \brief Create the test
   */
  PointToPointTest ();

  /**
   * \brief Run the test
   */
  virtual void DoRun (void);

private:
  /**
   * \brief Send one packet to the device specified
   *
   * \param device NetDevice to send to
   */
  void SendOnePacket (Ptr<PointToPointNetDevice> device);
};

class PointToPointLinkStateTest : public TestCase
{
public:
  PointToPointLinkStateTest ();
  virtual void DoRun (void);

private:
  void LinkStateChanged ();

  uint32_t m_linkChanges;
};

PointToPointLinkStateTest::PointToPointLinkStateTest ()
  : TestCase ("PointToPoint link state changes"),
    m_linkChanges (0)
{
}

void
PointToPointLinkStateTest::LinkStateChanged ()
{
  ++m_linkChanges;
}

void
PointToPointLinkStateTest::DoRun (void)
{
  Ptr<PointToPointNetDevice> device = CreateObject<PointToPointNetDevice> ();
  Ptr<PointToPointChannel> channel = CreateObject<PointToPointChannel> ();
  device->AddLinkChangeCallback (MakeCallback (&PointToPointLinkStateTest::LinkStateChanged, this));

  device->Attach (channel);
  NS_TEST_ASSERT_MSG_EQ (device->IsLinkUp (), true, "Attaching the channel must bring the link up");
  NS_TEST_ASSERT_MSG_EQ (m_linkChanges, 1, "Attaching the channel must report one link change");

  device->UpTheLink ();
  NS_TEST_ASSERT_MSG_EQ (m_linkChanges, 1, "Repeated link-up must not report a state change");

  device->DownTheLink ();
  NS_TEST_ASSERT_MSG_EQ (device->IsLinkUp (), false, "DownTheLink must bring the link down");
  NS_TEST_ASSERT_MSG_EQ (m_linkChanges, 2, "Link-down must report one link change");

  device->DownTheLink ();
  NS_TEST_ASSERT_MSG_EQ (m_linkChanges, 2, "Repeated link-down must not report a state change");

  device->UpTheLink ();
  NS_TEST_ASSERT_MSG_EQ (device->IsLinkUp (), true, "UpTheLink must bring the link up");
  NS_TEST_ASSERT_MSG_EQ (m_linkChanges, 3, "Link-up must report one link change");
}

PointToPointTest::PointToPointTest ()
  : TestCase ("PointToPoint")
{
}

void
PointToPointTest::SendOnePacket (Ptr<PointToPointNetDevice> device)
{
  Ptr<Packet> p = Create<Packet> ();
  device->Send (p, device->GetBroadcast (), 0x800);
}


void
PointToPointTest::DoRun (void)
{
  Ptr<Node> a = CreateObject<Node> ();
  Ptr<Node> b = CreateObject<Node> ();
  Ptr<PointToPointNetDevice> devA = CreateObject<PointToPointNetDevice> ();
  Ptr<PointToPointNetDevice> devB = CreateObject<PointToPointNetDevice> ();
  Ptr<PointToPointChannel> channel = CreateObject<PointToPointChannel> ();

  devA->Attach (channel);
  devA->SetAddress (Mac48Address::Allocate ());
  devA->SetQueue (CreateObject<DropTailQueue<Packet> > ());
  devB->Attach (channel);
  devB->SetAddress (Mac48Address::Allocate ());
  devB->SetQueue (CreateObject<DropTailQueue<Packet> > ());

  a->AddDevice (devA);
  b->AddDevice (devB);

  Simulator::Schedule (Seconds (1.0), &PointToPointTest::SendOnePacket, this, devA);

  Simulator::Run ();

  Simulator::Destroy ();
}

/**
 * \brief TestSuite for PointToPoint module
 */
class PointToPointTestSuite : public TestSuite
{
public:
  /**
   * \brief Constructor
   */
  PointToPointTestSuite ();
};

PointToPointTestSuite::PointToPointTestSuite ()
  : TestSuite ("devices-point-to-point", UNIT)
{
  AddTestCase (new PointToPointTest, TestCase::QUICK);
  AddTestCase (new PointToPointLinkStateTest, TestCase::QUICK);
}

static PointToPointTestSuite g_pointToPointTestSuite; //!< The testsuite
