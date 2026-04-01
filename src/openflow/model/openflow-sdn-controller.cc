/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
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
 * Author: Blake Hurd  <naimorai@gmail.com>
 */
#include "ns3/ipv4-address.h"
#include "openflow-packet.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <ns3/address.h>
#include <ns3/mac48-address.h>
#include <ns3/node-container.h>
#include <ns3/nstime.h>
#include <ns3/simulator.h>
#ifdef NS3_OPENFLOW

#include "openflow-sdn-controller.h"
#include "openflow-switch-net-device.h"
#include "ns3/log.h"
#include <ns3/double.h>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("OpenFlowSDNController");

namespace ofi{

uint8_t packetSize = 5;

void
MasterController::ReceiveFromSwitch (Ptr<OpenFlowSwitchNetDevice> swtch, ofpbuf* buffer)
{
  if (m_switches.find (swtch) == m_switches.end ())
    {
      NS_LOG_ERROR ("Can't receive from this switch, not registered to the Controller.");
      return;
    }

  // ofpbuf* newbuffer = buffer;
  // Ptr<OpenFlowSwitchNetDevice> sendswitch = swtch;
  // We have received any packet at this point, so we pull the header to figure out what type of packet we're handling.
  uint8_t type = GetPacketType (buffer);
  // uint32_t ports = swtch->GetNSwitchPorts();
  if (type == OFPT_PACKET_IN) // The switch didn't understand the packet it received, so it forwarded it to the controller.
    {
      ofp_packet_in * opi = (ofp_packet_in*)ofpbuf_try_pull (buffer, offsetof (ofp_packet_in, data));
      int port = ntohs (opi->in_port);

      // Create matching key.
      sw_flow_key key;
      key.wildcards = 0;
      flow_extract (buffer, port != -1 ? port : OFPP_NONE, &key.flow);

      // uint16_t out_port = OFPP_FLOOD;
      uint16_t out_port = 0;
      // uint16_t in_port = ntohs (key.flow.in_port);

      // If the destination address is learned to a specific port, find it.
      Mac48Address dst_addr;
      dst_addr.CopyFrom (key.flow.dl_dst);

      Ptr<Node> swtch_node = swtch->GetNode();
      // std::cout << "node:" << swtch_node->GetId() << "\tmac_addr:" << dst_addr << "\tip_addr:" << m_MacMaps[dst_addr].addr << std::endl;
      // // index是本机交换机的编号
      // uint32_t index = Ipv4GlobalRoutingHelper::SDSNCaculateFlow(swtch_node->GetId(), m_MacMaps[dst_addr].addr);

      // Create output-to-port action
      ofp_action_output x[1];
      x[0].type = htons (OFPAT_OUTPUT);
      x[0].len = htons (sizeof(ofp_action_output));
      x[0].port = out_port;

      // Create a new flow that outputs matched packets to a learned port, OFPP_FLOOD if there's no learned port.
      ofp_flow_mod* ofm = BuildFlow (key, opi->buffer_id, OFPFC_ADD, x, sizeof(x), OFP_FLOW_PERMANENT, m_expirationTime.IsZero () ? OFP_FLOW_PERMANENT : m_expirationTime.GetSeconds ());
      SendToSwitch (swtch, ofm, ofm->header.length);
    }
}

void MasterController::SetNode(Ptr<Node> node){
  m_node = node;
}

void SlaveController::SetNode(Ptr<Node> node){
  m_node = node;
}

void
SlaveController::ReceiveFromSwitch (Ptr<OpenFlowSwitchNetDevice> swtch, ofpbuf* buffer)
{
  if (m_switches.find (swtch) == m_switches.end ())
    {
      NS_LOG_ERROR ("Can't receive from this switch, not registered to the Controller.");
      return;
    }

  uint8_t type = GetPacketType (buffer);

  if (type == OFPT_PACKET_IN) 
    {
      ofp_packet_in * opi = (ofp_packet_in*)ofpbuf_try_pull (buffer, offsetof (ofp_packet_in, data));
      int port = ntohs (opi->in_port);

      sw_flow_key key;
      key.wildcards = 0;
      flow_extract (buffer, port != -1 ? port : OFPP_NONE, &key.flow);

      uint16_t out_port = ntohs (0);

      Mac48Address dst_addr;
      dst_addr.CopyFrom (key.flow.dl_dst);
      // if(dst_addr == m_master_node_info.macaddr){
      //   dst_addr = m_gws_node_info[0].macaddr;
      // } 

      // Create output-to-port action
      ofp_action_output x[1];
      x[0].type = htons (OFPAT_OUTPUT);
      x[0].len = htons (sizeof(ofp_action_output));
      x[0].port = out_port;

      // Create a new flow that outputs matched packets to a learned port, OFPP_FLOOD if there's no learned port.
      ofp_flow_mod* ofm = BuildFlow (key, opi->buffer_id, OFPFC_ADD, x, sizeof(x), OFP_FLOW_PERMANENT, m_expirationTime.IsZero () ? OFP_FLOW_PERMANENT : m_expirationTime.GetSeconds ());
      SendToSwitch (swtch, ofm, ofm->header.length);
    }
}

}
}

#endif // NS3_OPENFLOW
