/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright 2007 University of Washington
 * Copyright (C) 1999, 2000 Kunihiro Ishiguro, Toshiaki Takada
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
 * Authors:  Tom Henderson (tomhend@u.washington.edu)
 * 
 * Kunihiro Ishigura, Toshiaki Takada (GNU Zebra) are attributed authors
 * of the quagga 0.99.7/src/ospfd/ospf_spf.c code which was ported here
 */

#include <utility>
#include <vector>
#include <queue>
#include <algorithm>
#include <iostream>
#include "ns3/assert.h"
#include "ns3/fatal-error.h"
#include "ns3/log.h"
#include "ns3/node-list.h"
#include "ns3/ipv4.h"
#include "ns3/ipv4-routing-protocol.h"
#include "ns3/ipv4-list-routing.h"
#include "ns3/net-device-container.h"
#include "ns3/net-device.h"
#include "global-router-interface.h"
#include "sat-global-route-manager-impl.h"
#include "sat-candidate-queue.h"
#include "ipv4-global-routing.h"

#include "ns3/simulation-singleton.h"
#include "global-route-manager.h"

#define _rouXW
// #define _rouXW_backup_   // 注释后降低收敛时间

// #define _INTRAROU1_
// #define _INTRAROU2_

// #define _INTRAROUSIM_         // 与仿真平台对接时才使用
#define _INTERROU_

// #define rouLinkMin          // 注释后优化协同路由代码

// #define _LOGROU_
// #define _LOGTIME_
// #define _LOGTIME1_

namespace ns3 {

uint32_t ROUSTEP = 0;

NS_LOG_COMPONENT_DEFINE ("SatGlobalRouteManagerImpl");

/**
 * \brief Stream insertion operator.
 *
 * \param os the reference to the output stream
 * \param exit the exit node
 * \returns the reference to the output stream
 */
static std::ostream& 
operator<< (std::ostream& os, const SATSPFVertex::NodeExit_t& exit)
{
  os << "(" << exit.first << " ," << exit.second << ")";
  return os;
}

std::ostream& 
operator<< (std::ostream& os, const SATSPFVertex::ListOfSATSPFVertex_t& vs)
{
  typedef SATSPFVertex::ListOfSATSPFVertex_t::const_iterator CIter_t;
  os << "{";
  for (CIter_t iter = vs.begin (); iter != vs.end ();)
    {
      os << (*iter)->m_vertexId;
      if (++iter != vs.end ()) 
        {
          os << ", ";
        }
      else 
        { 
          break;
        }
    }
  os << "}";
  return os;
}


// ---------------------------------------------------------------------------
//
// SATSPFVertex Implementation
//
// ---------------------------------------------------------------------------

SATSPFVertex::SATSPFVertex () : 
  m_vertexType (VertexUnknown), 
  m_vertexId ("255.255.255.255"), 
  m_lsa (0),
  m_distanceFromRoot (SATSPF_INFINITY), 
  m_rootOif (SATSPF_INFINITY),
  m_nextHop ("0.0.0.0"),
  m_parents (),
  m_children (),
  m_vertexProcessed (false)
{
  NS_LOG_FUNCTION (this);
}

SATSPFVertex::SATSPFVertex (GlobalRoutingLSA* lsa) : 
  m_vertexId (lsa->GetLinkStateId ()),
  m_lsa (lsa),
  m_distanceFromRoot (SATSPF_INFINITY), 
  m_rootOif (SATSPF_INFINITY),
  m_nextHop ("0.0.0.0"),
  m_parents (),
  m_children (),
  m_vertexProcessed (false)
{
  NS_LOG_FUNCTION (this << lsa);

  if (lsa->GetLSType () == GlobalRoutingLSA::RouterLSA) 
    {
      NS_LOG_LOGIC ("Setting m_vertexType to VertexRouter");
      m_vertexType = SATSPFVertex::VertexRouter;
    }
  else if (lsa->GetLSType () == GlobalRoutingLSA::NetworkLSA) 
    { 
      NS_LOG_LOGIC ("Setting m_vertexType to VertexNetwork");
      m_vertexType = SATSPFVertex::VertexNetwork;
    }
}

SATSPFVertex::~SATSPFVertex ()
{
  NS_LOG_FUNCTION (this);

  NS_LOG_LOGIC ("Children vertices - " << m_children);
  NS_LOG_LOGIC ("Parent verteices - " << m_parents);

  // find this node from all its parents and remove the entry of this node
  // from all its parents
  for (ListOfSATSPFVertex_t::iterator piter = m_parents.begin (); 
       piter != m_parents.end ();
       piter++)
    {
      // remove the current vertex from its parent's children list. Check
      // if the size of the list is reduced, or the child<->parent relation
      // is not bidirectional
      uint32_t orgCount = (*piter)->m_children.size ();
      (*piter)->m_children.remove (this);
      uint32_t newCount = (*piter)->m_children.size ();
      if (orgCount > newCount)
        {
          NS_ASSERT_MSG (orgCount > newCount, "Unable to find the current vertex from its parents --- impossible!");
        }
    }

  // delete children
  while (m_children.size () > 0)
    {
      // pop out children one by one. Some children may disappear 
      // when deleting some other children in the list. As a result,
      // it is necessary to use pop to walk through all children, instead
      // of using iterator.
      //
      // Note that m_children.pop_front () is not necessary as this
      // p is removed from the children list when p is deleted
      SATSPFVertex* p = m_children.front ();
      // 'p' == 0, this child is already deleted by its other parent
      if (p == 0) continue;
      NS_LOG_LOGIC ("Parent vertex-" << m_vertexId << " deleting its child vertex-" << p->GetVertexId ());
      delete p;
      p = 0;
    }
  m_children.clear ();
  // delete parents
  m_parents.clear ();
  // delete root exit direction
  m_ecmpRootExits.clear ();

  NS_LOG_LOGIC ("Vertex-" << m_vertexId << " completed deleted");
}

void
SATSPFVertex::SetVertexType (SATSPFVertex::VertexType type)
{
  NS_LOG_FUNCTION (this << type);
  m_vertexType = type;
}

SATSPFVertex::VertexType
SATSPFVertex::GetVertexType (void) const
{
  NS_LOG_FUNCTION (this);
  return m_vertexType;
}

void
SATSPFVertex::SetVertexId (Ipv4Address id)
{
  NS_LOG_FUNCTION (this << id);
  m_vertexId = id;
}

Ipv4Address
SATSPFVertex::GetVertexId (void) const
{
  NS_LOG_FUNCTION (this);
  return m_vertexId;
}

void
SATSPFVertex::SetLSA (GlobalRoutingLSA* lsa)
{
  NS_LOG_FUNCTION (this << lsa);
  m_lsa = lsa;
}

GlobalRoutingLSA*
SATSPFVertex::GetLSA (void) const
{
  NS_LOG_FUNCTION (this);
  return m_lsa;
}

void
SATSPFVertex::SetDistanceFromRoot (uint32_t distance)
{
  NS_LOG_FUNCTION (this << distance);
  m_distanceFromRoot = distance;
}

uint32_t
SATSPFVertex::GetDistanceFromRoot (void) const
{
  NS_LOG_FUNCTION (this);
  return m_distanceFromRoot;
}

void
SATSPFVertex::SetParent (SATSPFVertex* parent)
{
  NS_LOG_FUNCTION (this << parent);

  // always maintain only one parent when using setter/getter methods
  m_parents.clear ();
  m_parents.push_back (parent);
}

SATSPFVertex*
SATSPFVertex::GetParent (uint32_t i) const
{
  NS_LOG_FUNCTION (this << i);

  // If the index i is out-of-range, return 0 and do nothing
  if (m_parents.size () <= i)
    {
      NS_LOG_LOGIC ("Index to SATSPFVertex's parent is out-of-range.");
      return 0;
    }
  ListOfSATSPFVertex_t::const_iterator iter = m_parents.begin ();
  while (i-- > 0) 
    {
      iter++;
    }
  return *iter;
}

void 
SATSPFVertex::MergeParent (const SATSPFVertex* v)
{
  NS_LOG_FUNCTION (this << v);

  NS_LOG_LOGIC ("Before merge, list of parents = " << m_parents);
  // combine the two lists first, and then remove any duplicated after
  m_parents.insert (m_parents.end (), 
                    v->m_parents.begin (), v->m_parents.end ());
  // remove duplication
  m_parents.sort ();
  m_parents.unique ();
  NS_LOG_LOGIC ("After merge, list of parents = " << m_parents);
}

void 
SATSPFVertex::SetRootExitDirection (Ipv4Address nextHop, int32_t id)
{
  NS_LOG_FUNCTION (this << nextHop << id);

  // always maintain only one root's exit
  m_ecmpRootExits.clear ();
  m_ecmpRootExits.push_back (NodeExit_t (nextHop, id));
  // update the following in order to be backward compatitable with
  // GetNextHop and GetOutgoingInterface methods
  m_nextHop = nextHop;
  m_rootOif = id;
}

void 
SATSPFVertex::SetRootExitDirection (SATSPFVertex::NodeExit_t exit)
{
  NS_LOG_FUNCTION (this << exit);
  SetRootExitDirection (exit.first, exit.second);
}

SATSPFVertex::NodeExit_t
SATSPFVertex::GetRootExitDirection (uint32_t i) const
{
  NS_LOG_FUNCTION (this << i);
  typedef ListOfNodeExit_t::const_iterator CIter_t;

  NS_ASSERT_MSG (i < m_ecmpRootExits.size (), "Index out-of-range when accessing SPFVertex::m_ecmpRootExits!");
  CIter_t iter = m_ecmpRootExits.begin ();
  while (i-- > 0) { iter++; }

  return *iter;
}

SATSPFVertex::NodeExit_t 
SATSPFVertex::GetRootExitDirection () const
{
  NS_LOG_FUNCTION (this);

  NS_ASSERT_MSG (m_ecmpRootExits.size () <= 1, "Assumed there is at most one exit from the root to this vertex");
  return GetRootExitDirection (0);
}

void 
SATSPFVertex::MergeRootExitDirections (const SATSPFVertex* vertex)
{
  NS_LOG_FUNCTION (this << vertex);

  // obtain the external list of exit directions
  //
  // Append the external list into 'this' and remove duplication afterward
  const ListOfNodeExit_t& extList = vertex->m_ecmpRootExits;
  m_ecmpRootExits.insert (m_ecmpRootExits.end (), 
                          extList.begin (), extList.end ());
  m_ecmpRootExits.sort ();
  m_ecmpRootExits.unique ();
}

void 
SATSPFVertex::InheritAllRootExitDirections (const SATSPFVertex* vertex)
{
  NS_LOG_FUNCTION (this << vertex);

  // discard all exit direction currently associated with this vertex,
  // and copy all the exit directions from the given vertex
  if (m_ecmpRootExits.size () > 0)
    {
      NS_LOG_WARN ("x root exit directions in this vertex are going to be discarded");
    }
  m_ecmpRootExits.clear ();
  m_ecmpRootExits.insert (m_ecmpRootExits.end (), 
                          vertex->m_ecmpRootExits.begin (), vertex->m_ecmpRootExits.end ());
}

uint32_t 
SATSPFVertex::GetNRootExitDirections () const
{
  NS_LOG_FUNCTION (this);
  return m_ecmpRootExits.size ();
}

uint32_t 
SATSPFVertex::GetNChildren (void) const
{
  NS_LOG_FUNCTION (this);
  return m_children.size ();
}

SATSPFVertex*
SATSPFVertex::GetChild (uint32_t n) const
{
  NS_LOG_FUNCTION (this << n);
  uint32_t j = 0;

  for ( ListOfSATSPFVertex_t::const_iterator i = m_children.begin ();
        i != m_children.end ();
        i++, j++)
    {
      if (j == n)
        {
          return *i;
        }
    }
  NS_ASSERT_MSG (false, "Index <n> out of range.");
  return 0;
}

uint32_t
SATSPFVertex::AddChild (SATSPFVertex* child)
{
  NS_LOG_FUNCTION (this << child);
  m_children.push_back (child);
  return m_children.size ();
}

void 
SATSPFVertex::SetVertexProcessed (bool value)
{
  NS_LOG_FUNCTION (this << value);
  m_vertexProcessed = value;
}

bool 
SATSPFVertex::IsVertexProcessed (void) const
{
  NS_LOG_FUNCTION (this);
  return m_vertexProcessed;
}

void
SATSPFVertex::ClearVertexProcessed (void)
{
  NS_LOG_FUNCTION (this);
  for (uint32_t i = 0; i < this->GetNChildren (); i++)
    {
      this->GetChild (i)->ClearVertexProcessed ();
    }
  this->SetVertexProcessed (false);
}

// ---------------------------------------------------------------------------
//
// SatGlobalRouteManagerLSDB Implementation
//
// ---------------------------------------------------------------------------

SatGlobalRouteManagerLSDB::SatGlobalRouteManagerLSDB ()
  :
    m_database (),
    m_extdatabase ()
{
  NS_LOG_FUNCTION (this);
}

SatGlobalRouteManagerLSDB::~SatGlobalRouteManagerLSDB ()
{
  NS_LOG_FUNCTION (this);
  LSDBMap_t::iterator i;
  for (i= m_database.begin (); i!= m_database.end (); i++)
    {
      NS_LOG_LOGIC ("free LSA");
      GlobalRoutingLSA* temp = i->second;
      delete temp;
    }
  for (uint32_t j = 0; j < m_extdatabase.size (); j++)
    {
      NS_LOG_LOGIC ("free ASexternalLSA");
      GlobalRoutingLSA* temp = m_extdatabase.at (j);
      delete temp;
    }
  NS_LOG_LOGIC ("clear map");
  m_database.clear ();
}

void
SatGlobalRouteManagerLSDB::Delete()
{
    for (auto i= m_database.begin (); i!= m_database.end (); i++)
    {
        NS_LOG_LOGIC ("free LSA");
        GlobalRoutingLSA* temp = i->second;
        delete temp;
    }
    m_database.clear ();
    for (uint32_t j = 0; j < m_extdatabase.size (); j++)
    {
      NS_LOG_LOGIC ("free ASexternalLSA");
      GlobalRoutingLSA* temp = m_extdatabase.at (j);
      delete temp;
    }
}

void
SatGlobalRouteManagerLSDB::Initialize ()
{
  NS_LOG_FUNCTION (this);
  LSDBMap_t::iterator i;
  for (i= m_database.begin (); i!= m_database.end (); i++)
    {
      GlobalRoutingLSA* temp = i->second;
      temp->SetStatus (GlobalRoutingLSA::LSA_SPF_NOT_EXPLORED);
    }
}

void
SatGlobalRouteManagerLSDB::Insert (Ipv4Address addr, GlobalRoutingLSA* lsa)
{
  NS_LOG_FUNCTION (this << addr << lsa);
  if (lsa->GetLSType () == GlobalRoutingLSA::ASExternalLSAs) 
    {
      m_extdatabase.push_back (lsa);
    } 
  else
    {
      m_database.insert (LSDBPair_t (addr, lsa));
    }
}

GlobalRoutingLSA*
SatGlobalRouteManagerLSDB::GetExtLSA (uint32_t index) const
{
  NS_LOG_FUNCTION (this << index);
  return m_extdatabase.at (index);
}

uint32_t
SatGlobalRouteManagerLSDB::GetNumExtLSAs () const
{
  NS_LOG_FUNCTION (this);
  return m_extdatabase.size ();
}

GlobalRoutingLSA*
SatGlobalRouteManagerLSDB::GetLSA (Ipv4Address addr) const
{
  NS_LOG_FUNCTION (this << addr);
//
// Look up an LSA by its address.
//
  LSDBMap_t::const_iterator i;
  for (i= m_database.begin (); i!= m_database.end (); i++)
    {
      if (i->first == addr)
        {
          return i->second;
        }
    }
  return 0;
}

GlobalRoutingLSA*
SatGlobalRouteManagerLSDB::GetLSAByLinkData (Ipv4Address addr) const
{
  NS_LOG_FUNCTION (this << addr);
//
// Look up an LSA by its address.
//
  LSDBMap_t::const_iterator i;
  for (i= m_database.begin (); i!= m_database.end (); i++)
    {
      GlobalRoutingLSA* temp = i->second;
// Iterate among temp's Link Records
      for (uint32_t j = 0; j < temp->GetNLinkRecords (); j++)
        {
          GlobalRoutingLinkRecord *lr = temp->GetLinkRecord (j);
          if ( lr->GetLinkType () == GlobalRoutingLinkRecord::TransitNetwork &&
               lr->GetLinkData () == addr)
            {
              return temp;
            }
        }
    }
  return 0;
}

// ---------------------------------------------------------------------------
//
// GlobalRouteManagerImpl Implementation
//
// ---------------------------------------------------------------------------

SatGlobalRouteManagerImpl::SatGlobalRouteManagerImpl () 
  :
    m_spfroot (0),
    m_deviceU (),
    m_clusterCost ()
{
  NS_LOG_FUNCTION (this);
  m_lsdb = new SatGlobalRouteManagerLSDB ();
}

SatGlobalRouteManagerImpl::~SatGlobalRouteManagerImpl ()
{
  NS_LOG_FUNCTION (this);
  if (m_lsdb)
    {
      delete m_lsdb;
    }

  NS_LOG_LOGIC ("clear map");
  m_deviceU.clear ();
  m_clusterCost.clear ();
}

void
SatGlobalRouteManagerImpl::DebugUseLsdb (SatGlobalRouteManagerLSDB* lsdb)
{
  NS_LOG_FUNCTION (this << lsdb);
  if (m_lsdb)
    {
      delete m_lsdb;
    }
  m_lsdb = lsdb;
}

void
SatGlobalRouteManagerImpl::DeleteGlobalRoutes ()
{
  NS_LOG_FUNCTION (this);
  NodeList::Iterator listEnd = NodeList::End ();
  for (NodeList::Iterator i = NodeList::Begin (); i != listEnd; i++)
    {
      Ptr<Node> node = *i;
      Ptr<GlobalRouter> router = node->GetObject<GlobalRouter> ();
      if (router == 0)
        {
          continue;
        }
      Ptr<Ipv4GlobalRouting> gr = router->GetRoutingProtocol ();
      uint32_t j = 0;
      uint32_t nRoutes = gr->GetNRoutes ();
      NS_LOG_LOGIC ("Deleting " << gr->GetNRoutes ()<< " routes from node " << node->GetId ());
      // Each time we delete route 0, the route index shifts downward
      // We can delete all routes if we delete the route numbered 0
      // nRoutes times
      for (j = 0; j < nRoutes; j++)
        {
          NS_LOG_LOGIC ("Deleting global route " << j << " from node " << node->GetId ());
          gr->RemoveRoute (0);
        }
      NS_LOG_LOGIC ("Deleted " << j << " global routes from node "<< node->GetId ());
    }
  if (m_lsdb)
    {
      NS_LOG_LOGIC ("Deleting LSDB, creating new one");
      delete m_lsdb;
      m_lsdb = new SatGlobalRouteManagerLSDB ();
    }
}


void
SatGlobalRouteManagerImpl::DeleteClusteringGlobalRoutes (NodeContainer& nodes)
{
    NS_LOG_FUNCTION (this);
    for (uint32_t i = 0; i < nodes.GetN(); i++)
    {
        Ptr<Node> node = nodes.Get(i);

        Ptr<GlobalRouter> router = node->GetObject<GlobalRouter> ();
        if (router == 0)
        {
          continue;
        }
        Ptr<Ipv4GlobalRouting> gr = router->GetRoutingProtocol ();
        uint32_t j = 0;
        uint32_t nRoutes = gr->GetNRoutes ();
        NS_LOG_LOGIC ("Deleting " << gr->GetNRoutes ()<< " routes from node " << node->GetId ());
        // Each time we delete route 0, the route index shifts downward
        // We can delete all routes if we delete the route numbered 0
        // nRoutes times
        for (j = 0; j < nRoutes; j++)
        {
          NS_LOG_LOGIC ("Deleting global route " << j << " from node " << node->GetId ());
          gr->RemoveRoute (0);
        }
        NS_LOG_LOGIC ("Deleted " << j << " global routes from node "<< node->GetId ());
    }
    
    ROUSTEP++;
    // std::cout << "Routing time step: " << ROUSTEP << std::endl;

    if (m_lsdb)
    {
      NS_LOG_LOGIC ("Deleting LSDB, creating new one");
      delete m_lsdb;
      m_lsdb = new SatGlobalRouteManagerLSDB ();
    }
}

void
SatGlobalRouteManagerImpl::DeleteRoutesForNodes (Ptr<Node> nodeSrc, Ptr<Node> nodeDst)
{
    Ptr<GlobalRouter> router = nodeSrc->GetObject<GlobalRouter> ();
    if (router == 0)
    {
        return;
    }
    Ptr<Ipv4GlobalRouting> gr = router->GetRoutingProtocol ();
    uint32_t j = 0;
    uint32_t nRoutes = gr->GetNRoutes ();
    uint32_t k = 0;
    NS_LOG_LOGIC ("Deleting routes from node " << nodeSrc->GetId () << " to node " << nodeDst->GetId ());
    // std::cout << "destIP: " << gr->GetRoute(j)->GetDest() << std::endl;
    uint32_t tmpDstID;
    for (j = 0; j < nRoutes; j++)
    {
        tmpDstID = GetIdFromIpSim(gr->GetRoute(j)->GetDest());
        if((tmpDstID != (uint32_t)-1) && (tmpDstID == nodeDst->GetId ()))
        {
            // std::cout << "id: " << tmpDstID << ", dstID: " << nodeDst->GetId () << std::endl;
            gr->RemoveRoute (j);
            nRoutes--;
            k++;
        }
    }
    NS_LOG_LOGIC ("Deleted " << k << " global routes from node "<< nodeSrc->GetId () << " to node " << nodeDst->GetId ());
}

void
SatGlobalRouteManagerImpl::DeleteLocalRoutes (Ptr<Node> node, uint32_t interface)
{
    NS_LOG_FUNCTION (this);
    Ptr<GlobalRouter> router = node->GetObject<GlobalRouter> ();
    if (router == 0)    return;
    Ptr<Ipv4GlobalRouting> gr = router->GetRoutingProtocol ();
    Ipv4RoutingTableEntry* table;
    for (uint32_t j = 0, i = 0; i < gr->GetNRoutes (); i++, j++)
    {
        NS_LOG_LOGIC ("Deleting global route " << j << " from node " << node->GetId ());
        
        table = gr->GetRoute(j);
        if(table->GetInterface() == interface)
        {
            // std::cout << "Deleting global route " << i << " from node " << node->GetId () << " with Nroutes " << gr->GetNRoutes () << std::endl;
            gr->RemoveRoute (j);
            j--;
        }
    }
    // std::cout << "Delete routeEntry for node " << node->GetId() << " interface " << interface << std::endl;
}

//
// In order to build the routing database, we need to walk the list of nodes
// in the system and look for those that support the GlobalRouter interface.
// These routers will export a number of Link State Advertisements (LSAs)
// that describe the links and networks that are "adjacent" (i.e., that are
// on the other side of a point-to-point link).  We take these LSAs and put
// add them to the Link State DataBase (LSDB) from which the routes will 
// ultimately be computed.
//
void
SatGlobalRouteManagerImpl::BuildGlobalRoutingDatabase () 
{
  NS_LOG_FUNCTION (this);
//
// Walk the list of nodes looking for the GlobalRouter Interface.  Nodes with
// global router interfaces are, not too surprisingly, our routers.
//
  NodeList::Iterator listEnd = NodeList::End ();
  for (NodeList::Iterator i = NodeList::Begin (); i != listEnd; i++)
    {
      Ptr<Node> node = *i;

      Ptr<GlobalRouter> rtr = node->GetObject<GlobalRouter> ();
//
// Ignore nodes that aren't participating in routing.
//
      if (!rtr)
        {
          continue;
        }
//
// You must call DiscoverLSAs () before trying to use any routing info or to
// update LSAs.  DiscoverLSAs () drives the process of discovering routes in
// the GlobalRouter.  Afterward, you may use GetNumLSAs (), which is a very
// computationally inexpensive call.  If you call GetNumLSAs () before calling 
// DiscoverLSAs () will get zero as the number since no routes have been 
// found.
//
      Ptr<Ipv4GlobalRouting> grouting = rtr->GetRoutingProtocol ();
      uint32_t numLSAs = rtr->DiscoverLSAs ();
      NS_LOG_LOGIC ("Found " << numLSAs << " LSAs");

      for (uint32_t j = 0; j < numLSAs; ++j)
        {
          GlobalRoutingLSA* lsa = new GlobalRoutingLSA ();
//
// This is the call to actually fetch a Link State Advertisement from the 
// router.
//
          rtr->GetLSA (j, *lsa);
          NS_LOG_LOGIC (*lsa);
//
// Write the newly discovered link state advertisement to the database.
//
          m_lsdb->Insert (lsa->GetLinkStateId (), lsa); 
        }
    }
}


void
SatGlobalRouteManagerImpl::ClusteringBuildGlobalRoutingDatabase(NodeContainer& nodes, bool RouMode)
{
    m_lsdb->Delete();
    //
    // Walk the list of nodes looking for the GlobalRouter Interface.  Nodes with
    // global router interfaces are, not too surprisingly, our routers.
    //
    // std::cout << "ClusteringBuildGlobalRoutingDatabase: " << "test1" << std::endl;
    for (uint32_t i = 0; i < nodes.GetN(); i++)
    {
        Ptr<Node> node = nodes.Get(i);

        Ptr<GlobalRouter> rtr = node->GetObject<GlobalRouter>();
        //
        // Ignore nodes that aren't participating in routing.
        //
        // std::cout << "ClusteringBuildGlobalRoutingDatabase: " << "test21" << std::endl;
        if (!rtr)
        {
            continue;
        }
        //
        // You must call DiscoverLSAs () before trying to use any routing info or to
        // update LSAs.  DiscoverLSAs () drives the process of discovering routes in
        // the GlobalRouter.  Afterward, you may use GetNumLSAs (), which is a very
        // computationally inexpensive call.  If you call GetNumLSAs () before calling
        // DiscoverLSAs () will get zero as the number since no routes have been
        // found.
        //
        Ptr<Ipv4GlobalRouting> grouting = rtr->GetRoutingProtocol();
        // std::cout << "ClusteringBuildGlobalRoutingDatabase: " << "test22" << std::endl;
        uint32_t numLSAs;
        numLSAs = rtr->DiscoverLSAs();
        // if(RouMode)
        // {
        //     numLSAs = rtr->DiscoverLSAs();
        // }
        // else
        // {
        //     numLSAs = rtr->SDSNDiscoverLSAs ();
        // }
        
        NS_LOG_LOGIC("Found " << numLSAs << " LSAs");
        // std::cout << "ClusteringBuildGlobalRoutingDatabase: " << "test23" << std::endl;
        for (uint32_t j = 0; j < numLSAs; ++j)
        {
            auto lsa = new GlobalRoutingLSA();
            //
            // This is the call to actually fetch a Link State Advertisement from the
            // router.
            //
            rtr->GetLSA(j, *lsa);
            NS_LOG_LOGIC(*lsa);
            //
            // Write the newly discovered link state advertisement to the database.
            //
            m_lsdb->Insert(lsa->GetLinkStateId(), lsa);
        }
        // std::cout << "ClusteringBuildGlobalRoutingDatabase: " << "test24" << std::endl;
    }
}

//
// For each node that is a global router (which is determined by the presence
// of an aggregated GlobalRouter interface), run the Dijkstra SPF calculation
// on the database rooted at that router, and populate the node forwarding
// tables.
//
// This function parallels RFC2328, Section 16.1.1, and quagga ospfd
//
// This calculation yields the set of intra-area routes associated
// with an area (called hereafter Area A).  A router calculates the
// shortest-path tree using itself as the root.  The formation
// of the shortest path tree is done here in two stages.  In the
// first stage, only links between routers and transit networks are
// considered.  Using the Dijkstra algorithm, a tree is formed from
// this subset of the link state database.  In the second stage,
// leaves are added to the tree by considering the links to stub
// networks.
//
// The area's link state database is represented as a directed graph.
// The graph's vertices are routers, transit networks and stub networks.
//
// The first stage of the procedure (i.e., the Dijkstra algorithm)
// can now be summarized as follows. At each iteration of the
// algorithm, there is a list of candidate vertices.  Paths from
// the root to these vertices have been found, but not necessarily
// the shortest ones.  However, the paths to the candidate vertex
// that is closest to the root are guaranteed to be shortest; this
// vertex is added to the shortest-path tree, removed from the
// candidate list, and its adjacent vertices are examined for
// possible addition to/modification of the candidate list.  The
// algorithm then iterates again.  It terminates when the candidate
// list becomes empty. 
//
void
SatGlobalRouteManagerImpl::InitializeRoutes ()
{
  NS_LOG_FUNCTION (this);
//
// Walk the list of nodes in the system.
//
  NS_LOG_INFO ("About to start SPF calculation");
  NodeList::Iterator listEnd = NodeList::End ();
  for (NodeList::Iterator i = NodeList::Begin (); i != listEnd; i++)
    {
      Ptr<Node> node = *i;
//
// Look for the GlobalRouter interface that indicates that the node is
// participating in routing.
//
      Ptr<GlobalRouter> rtr = 
        node->GetObject<GlobalRouter> ();

      uint32_t systemId = Simulator::GetSystemId ();
      // Ignore nodes that are not assigned to our systemId (distributed sim)
      if (node->GetSystemId () != systemId) 
        {
          continue;
        }

//
// if the node has a global router interface, then run the global routing
// algorithms.
//
      if (rtr && rtr->GetNumLSAs () )
        {
          SPFCalculate (rtr->GetRouterId ());
        }
    }
  NS_LOG_INFO ("Finished SPF calculation");
}

void
SatGlobalRouteManagerImpl::ClusteringInitializeRoutesSim(NodeContainer& nodes)
{
    for (uint32_t i = 0; i < nodes.GetN(); i++)
    {
        Ptr<Node> node = nodes.Get(i);

        Ptr<GlobalRouter> rtr = node->GetObject<GlobalRouter>();

        uint32_t systemId = Simulator::GetSystemId();

        if (node->GetSystemId() != systemId)
        {
            continue;
        }

        if (rtr && rtr->GetNumLSAs())
        {
            SPFCalculate (rtr->GetRouterId ());
        }
    }
}

void
SatGlobalRouteManagerImpl::ClusteringInitializeRoutes(NodeContainer& nodes, std::vector<std::vector<std::vector<std::pair<uint32_t, uint32_t>>>> BoundaryNode, bool RouMode, std::map<ns3::Ipv4Address, double> RlinkUtilization, std::map<ns3::Ipv4Address, std::vector<double>> clusterNetworkInf)
{
    // DeleteGlobalRoutes();
    // std::cout << "ClusteringInitializeRoutes test3" << std::endl;
    if(!m_deviceU.empty())
    {
        m_deviceU.clear ();
    }
    // 浅拷贝
    // m_deviceU = RlinkUtilization;

    // 深拷贝
    for (auto &entry : RlinkUtilization) 
    {
        m_deviceU[entry.first] = entry.second;
        // std::cout << "  Time: " << Simulator::Now() << ", satDIP: " << entry.first << ", linkUtilization: " << m_deviceU[entry.first] << std::endl;
    }

    m_maxClusterDiameter = 0.0;

    std::map<ns3::Ipv4Address, std::vector<double>>::iterator it = clusterNetworkInf.begin();
    // std::cout << "map size: " << clusterNetworkInf.size() << std::endl;
    for (; it != clusterNetworkInf.end(); it++)
    {
        std::vector<double> tmp = it->second;
        // std::cout << "it->second size: " << tmp.size() << std::endl;
        std::vector<double>::iterator ittmp = tmp.begin();
        m_clusterCost[it->first] = 1.0;

        m_clusterCost[it->first] *= *ittmp;
        ittmp++;
        m_clusterCost[it->first] *= *ittmp;
        m_maxClusterDiameter = *ittmp > m_maxClusterDiameter ? *ittmp : m_maxClusterDiameter;
        ittmp++;
        m_clusterCost[it->first] /= *ittmp;
    }

    for (uint32_t i = 0; i < nodes.GetN(); i++)
    {
        Ptr<Node> node = nodes.Get(i);
        //
        // Look for the GlobalRouter interface that indicates that the node is
        // participating in routing.
        //
        Ptr<GlobalRouter> rtr = node->GetObject<GlobalRouter>();
        // std::cout << "\nClusteringInitializeRoutes test2" << std::endl;
        uint32_t systemId = Simulator::GetSystemId();
        // Ignore nodes that are not assigned to our systemId (distributed sim)
        if (node->GetSystemId() != systemId)
        {
            continue;
        }

        //
        // if the node has a global router interface, then run the global routing
        // algorithms.
        //
        // std::cout << "ClusteringInitializeRoutes test1" << std::endl;
        if (rtr && rtr->GetNumLSAs())
        {
            if (!RouMode) {
                SPFCalculate(rtr->GetRouterId());
                Ptr<Ipv4GlobalRouting> grouting = rtr->GetRoutingProtocol ();
                // grouting->DealOpenFlowRoute(node);  // 后续需要处理，
            }
            else 
                SPFCalculateforCluster(rtr->GetRouterId(), BoundaryNode);
        }

    }
}

//通过节点获取IP地址
//通过第一个接口进行节点IP地址的获取可能会出现问题
Ipv4Address 
GetIPAddress(Ptr<ns3::Node> node, uint32_t interface)
{
    Ptr<Ipv4> ippp = node-> GetObject<Ipv4> ();
    //获得第1个接口的第0个地址--多数为第一个接口，第0个接口是本机127.0.0.1
    Ipv4Address ipaddress = ippp->GetAddress(interface,0).GetLocal();
    return ipaddress;
}

//通过端口号获取IP地址
Ipv4Address
GetIpFromInterface(Ptr<Node> node, uint32_t interface)
{
    Ipv4Address ip, ip_test;
    ip.GetZero();
    ip_test.GetZero();
    Ptr<Ipv4> ippp = node->GetObject<Ipv4> ();
    uint32_t interfacenumber = ippp->GetNInterfaces();                   // 得到该节点的接口数目
    for (uint32_t iterate = 1; iterate < interfacenumber; ++iterate)     // 遍历该节点的每一个接口
    {
        if(iterate == interface) 
        {
            if(ippp->GetNAddresses(iterate) == 0) continue;
            ip = ippp->GetAddress(iterate, 0).GetLocal();
            break;
        }
    }
    if(ip == ip_test) std::cout << "没办法通过端口号获取IP地址" << std::endl;
    return ip;
}

//通过本节点端口号获取相连邻节点IP地址
Ipv4Address
GetNeiIpFromInterface(Ptr<Node> node, int32_t interface)
{
    Ipv4Address ip;    
    ip.GetZero();

    int32_t curIf, neiIf;
    for(uint32_t k = 0; k < node->GetNDevices(); ++k)
    {
        Ptr<PointToPointNetDevice> p2pNetDevice = DynamicCast<PointToPointNetDevice>(node->GetDevice(k));
        Ptr<Ipv4> curIpv4 = node->GetObject<Ipv4>();
        curIf = curIpv4->GetInterfaceForDevice(p2pNetDevice);
        if((p2pNetDevice != nullptr) && (curIf == interface))
        {
            Ptr<Channel> channel = p2pNetDevice->GetChannel ();
            // 获取通道中连接的所有设备
            Ptr<NetDevice> otherDevice;
            if (channel->GetNDevices () == 2) // P2P 通道应该只有两个设备
            { 
                if (channel->GetDevice (0) == p2pNetDevice) 
                {
                    otherDevice = channel->GetDevice (1);
                } 
                else 
                {
                    otherDevice = channel->GetDevice (0);
                }
            }
            // 获取与给定设备相连的第一个设备所在的节点
            Ptr<Node> connectedNode = otherDevice->GetNode ();
            Ptr<Ipv4> neiIpv4 = connectedNode->GetObject<Ipv4>();
            neiIf = neiIpv4->GetInterfaceForDevice(otherDevice);
            ip = GetIpFromInterface(connectedNode, neiIf);
            break;
        }
    }
    return ip;
}

//通过IP地址获取全局节点号
uint32_t
GetIdFromIp(NodeContainer &nodes, Ipv4Address ip)
{
    for (uint32_t i = 0; i < nodes.GetN(); i++)
    {
        Ptr<Node> node = nodes.Get(i);
        Ptr<Ipv4> ippp = node->GetObject<Ipv4> ();
        if(!ippp) continue;
        uint32_t interfacenumber = ippp->GetNInterfaces();      // 得到该节点的接口数目 
       
        for (uint32_t iterate = 1; iterate < interfacenumber; iterate++)     // 遍历该节点的每一个接口
        {
            if(ippp->GetNAddresses(iterate) == 0) continue;
            Ipv4Address ipaddress = ippp->GetAddress(iterate, 0).GetLocal();    // 获取到某节点的接口IP地址
            // std::cout << "遍历中的IP为：" << ipaddress << std::endl;
            // std::cout << "输入的IP为：" << ip << std::endl;
            
            if (ipaddress == ip) return node->GetId();            // 将获取的所有接口IP与目的节点IP比较, 若相同则返回节点序号  
        }
    }
    // logMsg("ok");
    std::cout << "没办法通过IP地址找到节点 " << ip << std::endl;
    return -1;
}
// //通过全局节点号获取IP地址,只能在不是边界节点的情况下使用
// Ipv4Address
// GetIpFromId(uint32_t id)
// {
//     Ipv4Address ip, ip_test;
//     ip.GetZero();
//     ip_test.GetZero();
//     for (auto i = NodeList::Begin(); i != NodeList::End(); i++)
//     {
//         Ptr<Node> node = *i;
//         if(node->GetId() == id) ip = GetIPAddress(node);
//     }
//     if(ip == ip_test) std::cout << "没办法通过节点的全局号找到IP地址" << std::endl;
//     return ip;
// }

uint32_t
GetIdFromIpSim(Ipv4Address ip)
{
    for (auto i = NodeList::Begin(); i != NodeList::End(); ++i)
    {
        Ptr<Node> node = *i;
        Ptr<Ipv4> ippp = node->GetObject<Ipv4> ();
        if(!ippp) continue;
        uint32_t interfacenumber = ippp->GetNInterfaces();      // 得到该节点的接口数目           
        // std::cout << "下一跳ID号为：" << node->GetId()<< "  " << interfacenumber << std::endl;
        for (uint32_t iterate = 1; iterate < interfacenumber; iterate++)     // 遍历该节点的每一个接口
        {
            if(ippp->GetNAddresses(iterate) == 0) continue;
            Ipv4Address ipaddress = ippp->GetAddress(iterate, 0).GetLocal();    // 获取到某节点的接口IP地址
            // std::cout << "遍历中的IP为：" << ipaddress << std::endl;
            // std::cout << "输入的IP为：" << ip << std::endl;
            
            if (ipaddress == ip) return node->GetId();            // 将获取的所有接口IP与目的节点IP比较, 若相同则返回节点序号  
        }
    }
    // logMsg("ok");
    // std::cout << "没办法通过IP地址找到节点" << std::endl;
    return -1;
}

//通过全局节点获取节点
Ptr<Node>
GetNodeFromId(uint32_t id)
{
    bool flag = false;
    Ptr<Node> n = CreateObject<Node>();
    for (auto i = NodeList::Begin(); i != NodeList::End(); i++)
    {
        Ptr<Node> node = *i;
        if(node->GetId() == id) 
        {
            // std::cout << "成功通过节点的全局号找到节点" << std::endl;
            n = node;
            flag = true;
        }
    }
    if(!flag) std::cout << "未能成功通过节点的全局号找到节点" << std::endl;
    return n;
}

//通过IP获取节点
Ptr<Node>
GetNodeFromIP(Ipv4Address ip)
{    
    for (auto i = NodeList::Begin(); i != NodeList::End(); i++)
    {
        Ptr<Node> node = *i;
        Ptr<Ipv4> ippp = node->GetObject<Ipv4> ();
        if(!ippp) continue;
        uint32_t interfacenumber = ippp->GetNInterfaces();      // 得到该节点的接口数目 
       
        for (uint32_t iterate = 1; iterate < interfacenumber; iterate++)     // 遍历该节点的每一个接口
        {
            if(ippp->GetNAddresses(iterate) == 0) continue;
            Ipv4Address ipaddress = ippp->GetAddress(iterate, 0).GetLocal();    // 获取到某节点的接口IP地址        
            if (ipaddress == ip) return node;            // 将获取的所有接口IP与目的节点IP比较, 若相同则返回节点序号  
        }
    }

    std::cout << "没办法通过IP地址找到节点 " << ip << std::endl;
    return nullptr;
}

//获取下一跳地址
Ipv4RoutingTableEntry*
GetNextFromTable(Ptr<Node> ScrNode, Ptr<Node> DestNode)
{
    Ipv4Address test_ip("0.0.0.0");
    Ptr<GlobalRouter> router = ScrNode->GetObject<GlobalRouter>();
    // if(!router) std::cout << "无效的GlobalRouter" <<std::endl;
    Ptr<Ipv4GlobalRouting> gr = router->GetRoutingProtocol();
    Ipv4Address nextaddress;
    Ipv4RoutingTableEntry* table;
    Ptr<Ipv4> dstIppp = DestNode->GetObject<Ipv4> ();
    uint8_t buf[4];
    // std::cout << "NRoutes ：" << gr->GetNRoutes() <<std::endl;
    for(uint32_t i = 0; i < gr->GetNRoutes(); ++i)
    {
        // std::cout << "GetNextFromTable test 1." << " N: " << gr->GetNRoutes() << " i: " << i << " DestNodeId: " << DestNode->GetId() << std::endl;
        table = gr->GetRoute(i);
        Ipv4Address dIPAddress = table->GetDest();        
        dIPAddress.Serialize(buf);
        if(buf[3] == 0) continue;
        if(dIPAddress == test_ip) continue;       
        for(uint32_t j = 1; j < dstIppp->GetNInterfaces(); ++j)
        {
            if(dstIppp->GetNAddresses(j) == 0) continue;
            Ipv4Address dstAddress = dstIppp->GetAddress(j, 0).GetLocal();
            if(dIPAddress == dstAddress)
            {
                // std::cout << "找到下一跳地址了" << std::endl;
                nextaddress = table->GetGateway();
                // std::cout << "下一跳地址为:" << nextaddress << std::endl;
                return table;
            }
        }
    }
    NS_ASSERT(table);
    return table;
}

//获取卫星下一跳地址（优化查找时间）
Ipv4RoutingTableEntry*
GetSatNextFromTable(Ptr<Node> ScrNode, Ptr<Node> DestNode)
{
    Ipv4Address test_ip("0.0.0.0");
    Ptr<GlobalRouter> router = ScrNode->GetObject<GlobalRouter>();
    Ptr<Ipv4GlobalRouting> gr = router->GetRoutingProtocol();
    Ipv4RoutingTableEntry* table;
    Ptr<Ipv4> dstIppp = DestNode->GetObject<Ipv4> ();
    uint8_t buf[4];

    uint8_t dstBuf[4];
    Ipv4Address dstAddress = dstIppp->GetAddress(1, 0).GetLocal();
    dstAddress.Serialize(dstBuf);

    for(uint32_t i = 0; i < gr->GetNRoutes(); ++i)
    {
        table = gr->GetRoute(i);
        Ipv4Address dIPAddress = table->GetDest();        
        dIPAddress.Serialize(buf);
        if((buf[3] == 0) || (buf[2] != dstBuf[2]) || (buf[1] != dstBuf[1]) || (dIPAddress == test_ip))     
            continue;     
        for(uint32_t j = (buf[3]-1); j < dstIppp->GetNInterfaces(); ++j)
        {
            Ipv4Address dstAddress = dstIppp->GetAddress(j, 0).GetLocal();
            if(dIPAddress == dstAddress)
            {
                return table;
            }
        }
    }
    // NS_ASSERT(table);
    return nullptr;
}

//找寻邻居节点
NodeContainer 
FindNeibors(Ptr<Node> node)
{
    NodeContainer neighbor;
    uint32_t end = ISLnum < node->GetNDevices() ? ISLnum : node->GetNDevices() - 1;
    for(uint32_t i = 0; i <= end; ++i)
    {
        Ptr<PointToPointNetDevice> p2pNetDevice = DynamicCast<PointToPointNetDevice>(node->GetDevice(i));
        if(p2pNetDevice != nullptr)
        {
            if(!p2pNetDevice->IsSatLinkUp())   continue;
            Ptr<Channel> channel = p2pNetDevice->GetChannel ();
            Ptr<NetDevice> otherDevice;
            if (channel->GetNDevices () == 2) // P2P 通道应该只有两个设备
            { 
                if (channel->GetDevice (0) == p2pNetDevice) otherDevice = channel->GetDevice (1);
                else otherDevice = channel->GetDevice (0);
            }
            Ptr<Node> connectedNode = otherDevice->GetNode ();
            neighbor.Add(connectedNode);
        }
    }
    return neighbor;
}


//获取本节点interface
std::pair<int32_t, int32_t> 
GetInterface(Ptr<Node> ScrNode, Ptr<Node> DesNode)
{
    std::pair<int32_t, int32_t>  interface = {-1, -1};
    
    for(uint32_t i = 0; i < ScrNode->GetNDevices(); ++i)
    {
        Ptr<PointToPointNetDevice> p2pNetDevice = DynamicCast<PointToPointNetDevice>(ScrNode->GetDevice(i));
        if(p2pNetDevice != nullptr)
        {
            Ptr<Channel> channel = p2pNetDevice->GetChannel ();
            // 获取通道中连接的所有设备
            Ptr<NetDevice> otherDevice;
            if (channel->GetNDevices () == 2) // P2P 通道应该只有两个设备
            { 
                if (channel->GetDevice (0) == p2pNetDevice) 
                {
                    otherDevice = channel->GetDevice (1);
                } 
                else 
                {
                    otherDevice = channel->GetDevice (0);
                }
            }
            // 获取与给定设备相连的第一个设备所在的节点
            Ptr<Node> connectedNode = otherDevice->GetNode ();
            if(connectedNode == DesNode)
            {
                Ptr<Ipv4> ipv4 = ScrNode->GetObject<Ipv4>();
                interface.first = ipv4->GetInterfaceForDevice(p2pNetDevice);
                ipv4 = DesNode->GetObject<Ipv4>();
                interface.second = ipv4->GetInterfaceForDevice(otherDevice);
            }
        }
    }
    return interface;
}

// 判断是否为对应的边界节点
bool 
IsBoundaryNode(Ptr<Node> node, uint8_t NextClusterNum)
{
    bool flag = false;
    NodeContainer neibors = FindNeibors(node);
    for(uint32_t i = 0; i < neibors.GetN(); ++i)
    {
        if(neibors.Get(i)->m_ClusterNumber == (uint32_t)-1) continue;   // 跳过故障节点
        if(neibors.Get(i)->m_ClusterNumber == (uint32_t)NextClusterNum) 
        {
            flag =  true;
            break;
        }
    }
    // if(!flag) std::cout  << node->GetId() << "不是边界节点" <<std::endl;
    // else std::cout  << node->GetId() << "是边界节点" <<std::endl;
    return flag;
}

// 计算当前簇和邻居簇 簇间连路的平均带宽利用率
double
SatGlobalRouteManagerImpl::ComputeClusterLBU(uint32_t curClusterId, uint32_t neiClusterId, std::vector<std::vector<std::vector<std::pair<uint32_t, uint32_t>>>> BoundaryNode)
{
    int bSize = BoundaryNode[curClusterId][neiClusterId].size();
    double BoundaryLBUtmp = 0;
    double BoundaryLBUTotal = 0;

    Ptr<Node> BoundaryNode1;
    Ptr<Node> BoundaryNode2;

    int32_t interface;
    Ipv4Address BoundaryIP;

    for(int a = 0; a < bSize; a++)
    {// 获取所有簇间边界节点对，并求和链路带宽利用率
        BoundaryNode1 = GetNodeFromId(BoundaryNode[curClusterId][neiClusterId][a].first);
        BoundaryNode2 = GetNodeFromId(BoundaryNode[curClusterId][neiClusterId][a].second);

        if(BoundaryNode1->m_ClusterNumber == curClusterId)
        {// 进行判断，哪一个的边界节点是本簇的，情况一：BoundaryNode1为本簇节点
            // std::cout << "BoundaryNode1为本簇节点" << "ID号为：" << BoundaryNode1->GetId() << "\n";                    
            interface = GetInterface(BoundaryNode1, BoundaryNode2).first;
            BoundaryIP = GetIpFromInterface(BoundaryNode1, interface);
            
            if(m_deviceU.count(BoundaryIP) > 0)
            {
                BoundaryLBUtmp = m_deviceU[BoundaryIP];
                BoundaryLBUTotal += BoundaryLBUtmp;
            }
        }
        else if(BoundaryNode2->m_ClusterNumber == curClusterId)
        {// 进行判断，情况二：BoundaryNode2为本簇节点
            // std::cout << "BoundaryNode2为本簇节点" << "ID号为：" << BoundaryNode2->GetId() << "\n";                    
            interface = GetInterface(BoundaryNode2, BoundaryNode1).first;
            BoundaryIP = GetIpFromInterface(BoundaryNode2, interface);
            
            if(m_deviceU.count(BoundaryIP) > 0)
            {
                BoundaryLBUtmp = m_deviceU[BoundaryIP];
                BoundaryLBUTotal += BoundaryLBUtmp;
            }
        }       
    }

    BoundaryLBUTotal = BoundaryLBUTotal / double(bSize);    // 计算平均链路带宽利用率
    return BoundaryLBUTotal;
}

/*  
 *          | 
 * Region 1 | Region 3 
 *          |
 -------- Node ---------
 *          |
 * Region 2 | Region 4
 *          |
 */
bool 
InRegionforWalkerStar (uint32_t curSatId, uint32_t dstSatId, uint32_t boundSatId, uint32_t numOfOrbit, uint32_t satPerOrbit)
{
    uint32_t curSatOrbit, curSatNumber, dstSatOrbit, dstSatNumber, boundSatOrbit, boundSatNumber;
    uint32_t deltaNumber_CD;
    curSatOrbit = curSatId / satPerOrbit;
    curSatNumber = curSatId % satPerOrbit;
    dstSatOrbit = dstSatId / satPerOrbit;
    dstSatNumber = dstSatId % satPerOrbit;
    boundSatOrbit = boundSatId / satPerOrbit;
    boundSatNumber = boundSatId % satPerOrbit;    

    /* For Walker Star */
    if (curSatOrbit > dstSatOrbit)
    {
        if (curSatNumber > dstSatNumber)
        {
            deltaNumber_CD = curSatNumber - dstSatNumber;
            if (deltaNumber_CD < (double(satPerOrbit)/2.0))
            {// region 1
                if ((boundSatOrbit >= dstSatOrbit) && (boundSatOrbit <= curSatOrbit) && (boundSatNumber >= dstSatNumber) && (boundSatNumber <= curSatNumber))
                    return true;
                return false;
            }
            else if (deltaNumber_CD > (double(satPerOrbit)/2.0))
            {// region 2
                deltaNumber_CD = satPerOrbit - deltaNumber_CD;
                if ((boundSatOrbit >= dstSatOrbit) && (boundSatOrbit <= curSatOrbit) && ((boundSatNumber <= dstSatNumber) || (boundSatNumber >= curSatNumber)))
                    return true;
                return false;
            }
            else
            {// region 1 || 2
                if ((boundSatOrbit >= dstSatOrbit) && (boundSatOrbit <= curSatOrbit))
                    return true;
                return false;                
            } 
        }
        else if (curSatNumber < dstSatNumber)
        {
            deltaNumber_CD = dstSatNumber - curSatNumber;
            if (deltaNumber_CD < (double(satPerOrbit)/2.0))
            {// region 2
                if ((boundSatOrbit >= dstSatOrbit) && (boundSatOrbit <= curSatOrbit) && (boundSatNumber >= curSatNumber) && (boundSatNumber <= dstSatNumber))
                    return true;
                return false;
            }
            else if (deltaNumber_CD > (double(satPerOrbit)/2.0))
            {// region 1
                deltaNumber_CD = satPerOrbit - deltaNumber_CD;
                if ((boundSatOrbit >= dstSatOrbit) && (boundSatOrbit <= curSatOrbit) && ((boundSatNumber <= curSatNumber) || (boundSatNumber >= dstSatNumber)))
                    return true;
                return false;
            }
            else
            {// region 1 || 2
                if ((boundSatOrbit >= dstSatOrbit) && (boundSatOrbit <= curSatOrbit))
                    return true;
                return false;                
            } 
        }
        else
        {// region 1 || 2
            if ((boundSatOrbit >= dstSatOrbit) && (boundSatOrbit <= curSatOrbit) && (boundSatNumber == curSatNumber))
            {
                return true;
            }                
            return false;
        }
    }
    else if (curSatOrbit < dstSatOrbit)
    {
        if (curSatNumber > dstSatNumber)
        {
            deltaNumber_CD = curSatNumber - dstSatNumber;
            if (deltaNumber_CD < (double(satPerOrbit)/2.0))
            {// region 3
                if ((boundSatOrbit >= curSatOrbit) && (boundSatOrbit <= dstSatOrbit) && (boundSatNumber >= dstSatNumber) && (boundSatNumber <= curSatNumber))
                    return true;
                return false;
            }
            else if (deltaNumber_CD > (double(satPerOrbit)/2.0))
            {// region 4
                deltaNumber_CD = satPerOrbit - deltaNumber_CD;
                if ((boundSatOrbit >= curSatOrbit) && (boundSatOrbit <= dstSatOrbit) && ((boundSatNumber <= dstSatNumber) || (boundSatNumber >= curSatNumber)))
                    return true;
                return false;
            }
            else
            {// region 3 || 4
                if ((boundSatOrbit >= curSatOrbit) && (boundSatOrbit <= dstSatOrbit))
                    return true;
                return false;                
            } 
        }
        else if (curSatNumber < dstSatNumber)
        {
            deltaNumber_CD = dstSatNumber - curSatNumber;
            if (deltaNumber_CD < (double(satPerOrbit)/2.0))
            {// region 4
                if ((boundSatOrbit >= curSatOrbit) && (boundSatOrbit <= dstSatOrbit) && (boundSatNumber >= curSatNumber) && (boundSatNumber <= dstSatNumber))
                    return true;
                return false;
            }
            else if (deltaNumber_CD > (double(satPerOrbit)/2.0))
            {// region 3
                deltaNumber_CD = satPerOrbit - deltaNumber_CD;
                if ((boundSatOrbit >= curSatOrbit) && (boundSatOrbit <= dstSatOrbit) && ((boundSatNumber <= curSatNumber) || (boundSatNumber >= dstSatNumber)))
                    return true;
                return false;
            }
            else
            {// region 3 || 4
                if ((boundSatOrbit >= curSatOrbit) && (boundSatOrbit <= dstSatOrbit))
                    return true;
                return false;                
            } 
        }
        else
        {// region 3 || 4
            if ((boundSatOrbit >= curSatOrbit) && (boundSatOrbit <= dstSatOrbit) && (boundSatNumber == curSatNumber))
            {
                return true;
            }                
            return false;
        }
    }
    else
    {
        if (boundSatOrbit != curSatOrbit) return false;
        if (curSatNumber > dstSatNumber)
        {
            deltaNumber_CD = curSatNumber - dstSatNumber;
            if (deltaNumber_CD < (double(satPerOrbit)/2.0))
            {// region 1 || 3
                if ((boundSatNumber >= dstSatNumber) && (boundSatNumber <= curSatNumber))
                    return true;
                return false;
            }
            else if (deltaNumber_CD > (double(satPerOrbit)/2.0))
            {// region 2 || 4
                deltaNumber_CD = satPerOrbit - deltaNumber_CD;
                if ((boundSatNumber <= dstSatNumber) || (boundSatNumber >= curSatNumber))
                    return true;
                return false;
            }
            else
            {// region 1 || 2 || 3 || 4
                return true;             
            } 
        }
        else if (curSatNumber < dstSatNumber)
        {
            deltaNumber_CD = dstSatNumber - curSatNumber;
            if (deltaNumber_CD < (double(satPerOrbit)/2.0))
            {// region 2 || 4
                if ((boundSatNumber >= curSatNumber) && (boundSatNumber <= dstSatNumber))
                    return true;
                return false;
            }
            else if (deltaNumber_CD > (double(satPerOrbit)/2.0))
            {// region 1 || 3
                deltaNumber_CD = satPerOrbit - deltaNumber_CD;
                if ((boundSatNumber <= curSatNumber) || (boundSatNumber >= dstSatNumber))
                    return true;
                return false;
            }
            else
            {// region 1 || 2 || 3 || 4
                return true;                
            } 
        }
        else
        {// region 1 || 2 || 3 || 4
            if ((boundSatNumber == curSatNumber))
                return true;
            return false;
        }
    }

}

/*  
 *          | 
 * Region 1 | Region 3 
 *          |
 -------- Node ---------
 *          |
 * Region 2 | Region 4
 *          |
 */
bool 
InRegionforWalkerDelta (uint32_t curSatId, uint32_t dstSatId, uint32_t boundSatId, uint32_t numOfOrbit, uint32_t satPerOrbit)
{
    uint32_t curSatOrbit, curSatNumber, dstSatOrbit, dstSatNumber, boundSatOrbit, boundSatNumber;
    double deltaOrder_CD, deltaNumber_CD;
    curSatOrbit = curSatId / satPerOrbit;
    curSatNumber = curSatId % satPerOrbit;
    dstSatOrbit = dstSatId / satPerOrbit;
    dstSatNumber = dstSatId % satPerOrbit;
    boundSatOrbit = boundSatId / satPerOrbit;
    boundSatNumber = boundSatId % satPerOrbit;    

    /* For Walker Delta */
    if (curSatOrbit > dstSatOrbit)
    {
        deltaOrder_CD = curSatOrbit - dstSatOrbit;
        if(deltaOrder_CD < (double(numOfOrbit)/2.0))
        {
            if (curSatNumber > dstSatNumber)
            {
                deltaNumber_CD = curSatNumber - dstSatNumber;
                if (deltaNumber_CD < (double(satPerOrbit)/2.0))
                {// region 1
                    if ((boundSatOrbit >= dstSatOrbit) && (boundSatOrbit <= curSatOrbit) && (boundSatNumber >= dstSatNumber) && (boundSatNumber <= curSatNumber))
                        return true;
                    return false;
                }
                else if (deltaNumber_CD > (double(satPerOrbit)/2.0))
                {// region 2
                    deltaNumber_CD = satPerOrbit - deltaNumber_CD;
                    if ((boundSatOrbit >= dstSatOrbit) && (boundSatOrbit <= curSatOrbit) && ((boundSatNumber <= dstSatNumber) || (boundSatNumber >= curSatNumber)))
                        return true;
                    return false;
                }
                else
                {// region 1 || 2
                    if ((boundSatOrbit >= dstSatOrbit) && (boundSatOrbit <= curSatOrbit))
                        return true;
                    return false;                
                } 
            }
            else if (curSatNumber < dstSatNumber)
            {
                deltaNumber_CD = dstSatNumber - curSatNumber;
                if (deltaNumber_CD < (double(satPerOrbit)/2.0))
                {// region 2
                    if ((boundSatOrbit >= dstSatOrbit) && (boundSatOrbit <= curSatOrbit) && (boundSatNumber >= curSatNumber) && (boundSatNumber <= dstSatNumber))
                        return true;
                    return false;
                }
                else if (deltaNumber_CD > (double(satPerOrbit)/2.0))
                {// region 1
                    deltaNumber_CD = satPerOrbit - deltaNumber_CD;
                    if ((boundSatOrbit >= dstSatOrbit) && (boundSatOrbit <= curSatOrbit) && ((boundSatNumber <= curSatNumber) || (boundSatNumber >= dstSatNumber)))
                        return true;
                    return false;
                }
                else
                {// region 1 || 2
                    if ((boundSatOrbit >= dstSatOrbit) && (boundSatOrbit <= curSatOrbit))
                        return true;
                    return false;                
                } 
            }
            else
            {// region 1 || 2
                if ((boundSatOrbit >= dstSatOrbit) && (boundSatOrbit <= curSatOrbit) && (boundSatNumber == curSatNumber))
                {
                    return true;
                }                
                return false;
            }
        }
        else if(deltaOrder_CD > (double(numOfOrbit)/2.0))
        {
            deltaOrder_CD = numOfOrbit - deltaOrder_CD;
            if (curSatNumber > dstSatNumber)
            {
                deltaNumber_CD = curSatNumber - dstSatNumber;
                if (deltaNumber_CD < (double(satPerOrbit)/2.0))
                {// region 3
                    if (((boundSatOrbit <= dstSatOrbit) || (boundSatOrbit >= curSatOrbit)) && (boundSatNumber >= dstSatNumber) && (boundSatNumber <= curSatNumber))
                        return true;
                    return false;
                }
                else if (deltaNumber_CD > (double(satPerOrbit)/2.0))
                {// region 4
                    deltaNumber_CD = satPerOrbit - deltaNumber_CD;
                    if (((boundSatOrbit <= dstSatOrbit) || (boundSatOrbit >= curSatOrbit)) && ((boundSatNumber <= dstSatNumber) || (boundSatNumber >= curSatNumber)))
                        return true;
                    return false;
                }
                else
                {// region 3 || 4
                    if ((boundSatOrbit <= dstSatOrbit) || (boundSatOrbit >= curSatOrbit))
                        return true;
                    return false;                
                } 
            }
            else if (curSatNumber < dstSatNumber)
            {
                deltaNumber_CD = dstSatNumber - curSatNumber;
                if (deltaNumber_CD < (double(satPerOrbit)/2.0))
                {// region 4
                    if (((boundSatOrbit <= dstSatOrbit) || (boundSatOrbit >= curSatOrbit)) && (boundSatNumber >= curSatNumber) && (boundSatNumber <= dstSatNumber))
                        return true;
                    return false;
                }
                else if (deltaNumber_CD > (double(satPerOrbit)/2.0))
                {// region 3
                    deltaNumber_CD = satPerOrbit - deltaNumber_CD;
                    if (((boundSatOrbit <= dstSatOrbit) || (boundSatOrbit >= curSatOrbit)) && ((boundSatNumber <= curSatNumber) || (boundSatNumber >= dstSatNumber)))
                        return true;
                    return false;
                }
                else
                {// region 4 || 3
                    if ((boundSatOrbit <= dstSatOrbit) || (boundSatOrbit >= curSatOrbit))
                        return true;
                    return false;                
                } 
            }
            else
            {// region 3 || 4
                if (((boundSatOrbit <= dstSatOrbit) || (boundSatOrbit >= curSatOrbit)) && (boundSatNumber == curSatNumber))
                {
                    return true;
                }                
                return false;
            }
        }
        else
        {
            if (curSatNumber > dstSatNumber)
            {
                deltaNumber_CD = curSatNumber - dstSatNumber;
                if (deltaNumber_CD < (double(satPerOrbit)/2.0))
                {// region 1 || 3
                    if ((boundSatNumber >= dstSatNumber) && (boundSatNumber <= curSatNumber))
                        return true;
                    return false;
                }
                else if (deltaNumber_CD > (double(satPerOrbit)/2.0))
                {// region 2 || 4
                    deltaNumber_CD = satPerOrbit - deltaNumber_CD;
                    if ((boundSatNumber <= dstSatNumber) || (boundSatNumber >= curSatNumber))
                        return true;
                    return false;
                }
                else
                {// region 1 || 2 || 3 || 4
                    return true;            
                } 
            }
            else if (curSatNumber < dstSatNumber)
            {
                deltaNumber_CD = dstSatNumber - curSatNumber;
                if (deltaNumber_CD < (double(satPerOrbit)/2.0))
                {// region 2 || 4
                    if ((boundSatNumber >= curSatNumber) && (boundSatNumber <= dstSatNumber))
                        return true;
                    return false;
                }
                else if (deltaNumber_CD > (double(satPerOrbit)/2.0))
                {// region 1 || 3
                    deltaNumber_CD = satPerOrbit - deltaNumber_CD;
                    if ((boundSatNumber <= curSatNumber) || (boundSatNumber >= dstSatNumber))
                        return true;
                    return false;
                }
                else
                {// region 2 || 1 || 4 || 3
                    return true;               
                } 
            }
            else
            {// region 1 || 2 || 3 || 4
                if ((boundSatNumber == curSatNumber))
                {
                    return true; 
                }
                return false;
            }
        }
    }
    else if(curSatOrbit < dstSatOrbit)
    {
        deltaOrder_CD = dstSatOrbit - curSatOrbit;
        if(deltaOrder_CD < (double(numOfOrbit)/2.0))
        {
            if (curSatNumber > dstSatNumber)
            {
                deltaNumber_CD = curSatNumber - dstSatNumber;
                if (deltaNumber_CD < (double(satPerOrbit)/2.0))
                {// region 3
                    if ((boundSatOrbit <= dstSatOrbit) && (boundSatOrbit >= curSatOrbit) && (boundSatNumber >= dstSatNumber) && (boundSatNumber <= curSatNumber))
                        return true;
                    return false;
                }
                else if (deltaNumber_CD > (double(satPerOrbit)/2.0))
                {// region 4
                    deltaNumber_CD = satPerOrbit - deltaNumber_CD;
                    if ((boundSatOrbit <= dstSatOrbit) && (boundSatOrbit >= curSatOrbit) && ((boundSatNumber <= dstSatNumber) || (boundSatNumber >= curSatNumber)))
                        return true;
                    return false;
                }
                else
                {// region 3 || 4
                    if ((boundSatOrbit <= dstSatOrbit) && (boundSatOrbit >= curSatOrbit))
                        return true;
                    return false;                
                } 
            }
            else if (curSatNumber < dstSatNumber)
            {
                deltaNumber_CD = dstSatNumber - curSatNumber;
                if (deltaNumber_CD < (double(satPerOrbit)/2.0))
                {// region 4
                    if ((boundSatOrbit <= dstSatOrbit) && (boundSatOrbit >= curSatOrbit) && (boundSatNumber >= curSatNumber) && (boundSatNumber <= dstSatNumber))
                        return true;
                    return false;
                }
                else if (deltaNumber_CD > (double(satPerOrbit)/2.0))
                {// region 3
                    deltaNumber_CD = satPerOrbit - deltaNumber_CD;
                    if ((boundSatOrbit <= dstSatOrbit) && (boundSatOrbit >= curSatOrbit) && ((boundSatNumber <= curSatNumber) || (boundSatNumber >= dstSatNumber)))
                        return true;
                    return false;
                }
                else
                {// region 4 || 3
                    if ((boundSatOrbit <= dstSatOrbit) && (boundSatOrbit >= curSatOrbit))
                        return true;
                    return false;                
                } 
            }
            else
            {// region 3 || 4
                if ((boundSatOrbit <= dstSatOrbit) && (boundSatOrbit >= curSatOrbit) && (boundSatNumber == curSatNumber))
                {
                    return true;
                }                
                return false;
            }
        }
        else if(deltaOrder_CD > (double(numOfOrbit)/2.0))
        {
            deltaOrder_CD = numOfOrbit - deltaOrder_CD;
            if (curSatNumber > dstSatNumber)
            {
                deltaNumber_CD = curSatNumber - dstSatNumber;
                if (deltaNumber_CD < (double(satPerOrbit)/2.0))
                {// region 1
                    if (((boundSatOrbit >= dstSatOrbit) || (boundSatOrbit <= curSatOrbit)) && (boundSatNumber >= dstSatNumber) && (boundSatNumber <= curSatNumber))
                        return true;
                    return false;
                }
                else if (deltaNumber_CD > (double(satPerOrbit)/2.0))
                {// region 2
                    deltaNumber_CD = satPerOrbit - deltaNumber_CD;
                    if (((boundSatOrbit >= dstSatOrbit) || (boundSatOrbit <= curSatOrbit)) && ((boundSatNumber <= dstSatNumber) || (boundSatNumber >= curSatNumber)))
                        return true;
                    return false;
                }
                else
                {// region 1 || 2
                    if ((boundSatOrbit >= dstSatOrbit) || (boundSatOrbit <= curSatOrbit))
                        return true;
                    return false;                
                } 
            }
            else if (curSatNumber < dstSatNumber)
            {
                deltaNumber_CD = dstSatNumber - curSatNumber;
                if (deltaNumber_CD < (double(satPerOrbit)/2.0))
                {// region 2
                    if (((boundSatOrbit >= dstSatOrbit) || (boundSatOrbit <= curSatOrbit)) && (boundSatNumber >= curSatNumber) && (boundSatNumber <= dstSatNumber))
                        return true;
                    return false;
                }
                else if (deltaNumber_CD > (double(satPerOrbit)/2.0))
                {// region 1
                    deltaNumber_CD = satPerOrbit - deltaNumber_CD;
                    if (((boundSatOrbit >= dstSatOrbit) || (boundSatOrbit <= curSatOrbit)) && ((boundSatNumber <= curSatNumber) || (boundSatNumber >= dstSatNumber)))
                        return true;
                    return false;
                }
                else
                {// region 2 || 1
                    if ((boundSatOrbit >= dstSatOrbit) || (boundSatOrbit <= curSatOrbit))
                        return true;
                    return false;                
                } 
            }
            else
            {// region 2 || 1
                if (((boundSatOrbit >= dstSatOrbit) || (boundSatOrbit <= curSatOrbit)) && (boundSatNumber == curSatNumber))
                {
                    return true;
                }                
                return false;
            }
        }
        else
        {
            if (curSatNumber > dstSatNumber)
            {
                deltaNumber_CD = curSatNumber - dstSatNumber;
                if (deltaNumber_CD < (double(satPerOrbit)/2.0))
                {// region 1 || 3
                    if ((boundSatNumber >= dstSatNumber) && (boundSatNumber <= curSatNumber))
                        return true;
                    return false;
                }
                else if (deltaNumber_CD > (double(satPerOrbit)/2.0))
                {// region 2 || 4
                    deltaNumber_CD = satPerOrbit - deltaNumber_CD;
                    if ((boundSatNumber <= dstSatNumber) || (boundSatNumber >= curSatNumber))
                        return true;
                    return false;
                }
                else
                {// region 1 || 2 || 3 || 4
                    return true;            
                } 
            }
            else if (curSatNumber < dstSatNumber)
            {
                deltaNumber_CD = dstSatNumber - curSatNumber;
                if (deltaNumber_CD < (double(satPerOrbit)/2.0))
                {// region 2 || 4
                    if ((boundSatNumber >= curSatNumber) && (boundSatNumber <= dstSatNumber))
                        return true;
                    return false;
                }
                else if (deltaNumber_CD > (double(satPerOrbit)/2.0))
                {// region 1 || 3
                    deltaNumber_CD = satPerOrbit - deltaNumber_CD;
                    if ((boundSatNumber <= curSatNumber) || (boundSatNumber >= dstSatNumber))
                        return true;
                    return false;
                }
                else
                {// region 2 || 1 || 4 || 3
                    return true;               
                } 
            }
            else
            {// region 1 || 2 || 3 || 4
                if ((boundSatNumber == curSatNumber))
                    return true;
                return false;             
            }
        }
    }
    else
    {
        if (curSatNumber > dstSatNumber)
        {
            deltaNumber_CD = curSatNumber - dstSatNumber;
            if (deltaNumber_CD < (double(satPerOrbit)/2.0))
            {// region 1 || 3
                if ((boundSatOrbit == curSatOrbit) && (boundSatNumber >= dstSatNumber) && (boundSatNumber <= curSatNumber))
                    return true;
                return false;
            }
            else if (deltaNumber_CD > (double(satPerOrbit)/2.0))
            {// region 2 || 4
                deltaNumber_CD = satPerOrbit - deltaNumber_CD;
                if ((boundSatOrbit == curSatOrbit) && ((boundSatNumber <= dstSatNumber) || (boundSatNumber >= curSatNumber)))
                    return true;
                return false;
            }
            else
            {// region 1 || 2 || 3 || 4
                if ((boundSatOrbit == curSatOrbit))
                    return true;
                return false;                
            } 
        }
        else if (curSatNumber < dstSatNumber)
        {
            deltaNumber_CD = dstSatNumber - curSatNumber;
            if (deltaNumber_CD < (double(satPerOrbit)/2.0))
            {// region 2 || 4
                if ((boundSatOrbit == curSatOrbit) && (boundSatNumber >= curSatNumber) && (boundSatNumber <= dstSatNumber))
                    return true;
                return false;
            }
            else if (deltaNumber_CD > (double(satPerOrbit)/2.0))
            {// region 1 || 3
                deltaNumber_CD = satPerOrbit - deltaNumber_CD;
                if ((boundSatOrbit == curSatOrbit) && ((boundSatNumber <= curSatNumber) || (boundSatNumber >= dstSatNumber)))
                    return true;
                return false;
            }
            else
            {// region 1 || 2 || 4 || 3
                if ((boundSatOrbit == curSatOrbit))
                    return true;
                return false;                
            } 
        }
        else
        {// curnode
            if ((boundSatOrbit == curSatOrbit) && (boundSatNumber == curSatNumber))
            {
                return true;
            }                
            return false;
        }
    }
}

int32_t GetBackupInterface(Ptr<Node> CurNode, Ptr<Node> DstNode, int32_t nxtIf, uint8_t index, bool consType, uint32_t numOfOrbit, uint32_t satPerOrbit)
{
    // Ptr<Node> curSat = GetNodeFromId(curSatId + index);
    uint32_t curSatId = CurNode->GetId() - index;
    uint32_t dstSatId = DstNode->GetId() - index;

    uint32_t backUpNeiSatId;
    int32_t backUpInterface;

    NodeContainer neibors = FindNeibors(CurNode);
    for(uint32_t i = 0; i < neibors.GetN(); ++i)
    {       
        backUpInterface = GetInterface(CurNode, neibors.Get(i)).first;
        if(backUpInterface == nxtIf) continue;

        backUpNeiSatId = neibors.Get(i)->GetId() - index;
        if(!consType && InRegionforWalkerStar(curSatId, dstSatId, backUpNeiSatId, numOfOrbit, satPerOrbit))
            return backUpInterface;
        else if(InRegionforWalkerDelta(curSatId, dstSatId, backUpNeiSatId, numOfOrbit, satPerOrbit))
            return backUpInterface;
    }
    return -1;
}

void
SatGlobalRouteManagerImpl::AddClusterRoutingTables(
                                                    NodeContainer& cluster,
                                                    NodeContainer& GlobalNode,
                                                    NodeContainer& AbstractClusterNodes,
                                                    std::vector<std::vector<std::vector<std::pair<uint32_t, uint32_t>>>> BoundaryNode,
                                                    bool consType,
                                                    uint32_t numOfOrbit, 
                                                    uint32_t satPerOrbit)
{
    // 初始化参数
    Ptr<Node> node = GlobalNode.Get(0);             // 目的节点
    uint8_t NumInterface = 0;                       // 目的节点端口数
    uint32_t ClusterNum = cluster.Get(0)->m_ClusterNumber;      // 当前节点簇ID
    uint32_t OtherClusterNum = 0;                                        // 目的节点簇ID
    uint32_t NextClusterNum = 0;                                         // 下一跳节点簇ID
    Ipv4Address AbstractClusterNextIp;              // 抽象图下一跳节点IP

    Ptr<Node> DestBoundaryNode1;                    // 本簇边界节点
    Ptr<Node> DestBoundaryNode2;                    // 下一簇边界节点
    Ptr<Node> DestBoundaryNode = nullptr;
    #ifdef rouLinkMin
    Ptr<Node> DestBoundaryNodeTotal;
    #endif

    uint8_t index = GlobalNode.Get(0)->GetId();     // 由于地面站存在，获取卫星起始ID
    uint32_t sates_num = GlobalNode.GetN();         // 卫星总数

    for(uint32_t i = 0; i < sates_num; i++)
    {// 进行全局节点的遍历，寻找不是本簇的节点
        #ifdef _LOGTIME_
        auto t1_start = std::chrono::steady_clock::now();
        #endif        
        node = GlobalNode.Get(i);
        uint32_t dstSatID = i;      // = node->GetId() - index
        OtherClusterNum = node->m_ClusterNumber;
        if((OtherClusterNum == ClusterNum) || (OtherClusterNum == (uint32_t)-1)) continue;     // node is in the same cluster or fault
        #ifdef _LOGTIME_
        auto t11_end = std::chrono::steady_clock::now();
        #endif
        
        // If node is not in the same cluster, compute the route.
        AbstractClusterNextIp = GetNextFromTable(AbstractClusterNodes.Get(ClusterNum), 
                                                 AbstractClusterNodes.Get(OtherClusterNum))->GetGateway();
        #ifdef _LOGTIME_
        auto t12_end = std::chrono::steady_clock::now();
        #endif
        uint32_t NextClusterID = GetIdFromIp(AbstractClusterNodes, AbstractClusterNextIp);
        if(NextClusterID == (uint32_t)-1)    continue;
        NextClusterNum = NextClusterID - sates_num - index;
        #ifdef _LOGTIME_
        auto t13_end = std::chrono::steady_clock::now();
        #endif        
        NumInterface = (uint8_t)node->GetObject<Ipv4>()->GetNInterfaces();
        #ifdef _LOGTIME_
        auto t14_end = std::chrono::steady_clock::now();
        std::cout << "\nt1: " << std::chrono::duration<double>(t14_end - t1_start).count();
        std::cout << ",\t t11: " << std::chrono::duration<double>(t11_end - t1_start).count() << \
                        ", t12: " << std::chrono::duration<double>(t12_end - t11_end).count() << \
                        ", t13: " << std::chrono::duration<double>(t13_end - t12_end).count() << \
                        ", t14: " << std::chrono::duration<double>(t14_end - t13_end).count() << std::endl;
        #endif
        
        #ifdef _LOGROU_
        // std::cout << "当前节点的簇编号为：" << ClusterNum << std::endl;
        // std::cout << "目的节点的簇编号为：" << OtherClusterNum << std::endl;
        std::cout << "下一跳的抽象节点簇IP为：" << AbstractClusterNextIp << std::endl;
        std::cout << "下一跳的簇编号为：" << NextClusterNum << "\n";
        #endif
        
        for(uint32_t j = 0; j < cluster.GetN(); ++j)
        {// 将本簇簇内节点的路由表进行补全
            uint32_t curSatID = cluster.Get(j)->GetId() - index; // ID从0开始
            NodeContainer neighbor = FindNeibors(cluster.Get(j));
            #ifdef _LOGTIME_
            auto t2_start = std::chrono::steady_clock::now();
            #endif
            if(IsBoundaryNode(cluster.Get(j), (uint8_t)NextClusterNum))
            {// 判断出来当前节点是本簇边界节点，使用BoundaryNode的邻居簇边界节点补全路由表
                int dstFlag = -1;
                int regionFlag = -1;
                int otherFlag = -1;
                for(uint32_t k = 0; k < neighbor.GetN(); ++k)
                {
                    bool inRegion = false;
                    if(!consType)
                        inRegion = InRegionforWalkerStar(curSatID, i, neighbor.Get(k)->GetId()-index, numOfOrbit, satPerOrbit);
                    else
                        inRegion = InRegionforWalkerDelta(curSatID, i, neighbor.Get(k)->GetId()-index, numOfOrbit, satPerOrbit);
                    
                    if(neighbor.Get(k) == node)
                    {// 如果节点非本簇邻居是目的节点
                        dstFlag = (int)k;
                        break;
                    }
                    if(inRegion)
                    {
                        regionFlag = (int)k;
                        break;
                    }
                    else
                    {
                        otherFlag = (int)k;
                    }
                }
                
                Ptr<GlobalRouter> router = cluster.Get(j)->GetObject<GlobalRouter>();
                Ptr<Ipv4GlobalRouting> gr = router->GetRoutingProtocol();    
                uint32_t kk = 0;  
                int32_t interface1 = 0, interface2 = 0, backupInterface1 = 0;           
                if(dstFlag >= 0) 
                {
                    kk = (uint32_t)dstFlag;
                }
                else if(regionFlag >= 0)
                {
                    kk = (uint32_t)regionFlag;
                }
                else
                {// 边界节点没有非本簇邻居是目的节点 且无邻居位于矩形域内
                    kk = (uint32_t)otherFlag;
                }
                interface1 = GetInterface(cluster.Get(j), neighbor.Get(kk)).first;
                interface2 = GetInterface(cluster.Get(j), neighbor.Get(kk)).second;

                for(uint8_t m = 0; m < NumInterface; m++)
                {
                    if((m != interface1) && (m != interface2))
                    {// 选取矩形域邻居作为备份路径
                        backupInterface1 = m;
                        break;
                    }
                }

                for(uint8_t n = 1; n < NumInterface; ++n)
                {// 添加路由表项
                    // 添加主路由表项
                    gr->AddHostRouteTo(GetIPAddress(node, (uint32_t)n), 
                                                    GetIpFromInterface(neighbor.Get(kk), interface2), interface1);
                    if (backupInterface1 > 0)
                    {// 添加备份路由表项
                        Ipv4Address NextHopBackUpAddress = GetNeiIpFromInterface(cluster.Get(j), backupInterface1);                            
                        gr->AddHostRouteTo(GetIPAddress(node, (uint32_t)n), NextHopBackUpAddress, backupInterface1);    
                    }
                }
            }
            else
            {// 不是边界节点，遍历路由表找出下一跳进行补全
                int bSize = BoundaryNode[ClusterNum][NextClusterNum].size();
                if(bSize == 0) 
                {
                    continue;
                    std::cout << "E: 簇间无边界节点，找不到簇间路由表" << std::endl;
                }

                #ifdef _LOGROU_
                std::cout << "ClusterNum: " << ClusterNum << "\tNextClusterNum: " << NextClusterNum << "\tBoundaryNode[ClusterNum][NextClusterNum] size: "<< bSize << std::endl;
                #endif

                double DestBoundaryUtmp = 100.0;
                #ifdef rouLinkMin
                double DestBoundaryUtotal = 100.0;
                DestBoundaryUtotal = m_maxClusterDiameter > DestBoundaryUtotal ? m_maxClusterDiameter : DestBoundaryUtotal; // 应为簇直径最大值
                #endif

                double DestBoundaryU = 1.0;
                bool regionFlag1;

                #ifdef _LOGTIME_
                auto t3_start = std::chrono::steady_clock::now();
                #endif
                int inRegionCnt = 0;
                DestBoundaryNode = nullptr;
                for(int a = 0; a < bSize; a++)
                {// 遍历所有簇间边界节点，选择边界转发节点（矩形域 + 最小链路带宽利用率）
                    #ifdef _LOGTIME_
                    auto t31_start = std::chrono::steady_clock::now();
                    #endif  

                    #ifdef _LOGTIME1_
                    auto t311_start = std::chrono::steady_clock::now();
                    #endif               
                    // DestBoundaryNode1 = GetNodeFromId(BoundaryNode[ClusterNum][NextClusterNum][a].first);   // DestBoundaryNode1为本簇节点
                    uint32_t boundaryNodeID1 = BoundaryNode[ClusterNum][NextClusterNum][a].first - index;
                    DestBoundaryNode1 = GlobalNode.Get(boundaryNodeID1);

                    #ifdef _LOGTIME1_
                    auto t311_end = std::chrono::steady_clock::now();
                    std::cout << "t311: " << std::chrono::duration<double>(t311_end - t311_start).count();
                    #endif  

                    #ifdef _LOGTIME1_
                    auto t312_start = std::chrono::steady_clock::now();
                    #endif                                      
                    // DestBoundaryNode2 = GetNodeFromId(BoundaryNode[ClusterNum][NextClusterNum][a].second);  // DestBoundaryNode2为非本簇节点
                    uint32_t boundaryNodeID2 = BoundaryNode[ClusterNum][NextClusterNum][a].second - index;
                    DestBoundaryNode2 = GlobalNode.Get(boundaryNodeID2);
                    #ifdef _LOGTIME1_
                    auto t312_end = std::chrono::steady_clock::now();
                    std::cout << ", t312: " << std::chrono::duration<double>(t312_end - t312_start).count();
                    #endif

                    #ifdef _LOGTIME1_
                    auto t313_start = std::chrono::steady_clock::now();
                    #endif                    
                    int32_t interface = GetInterface(DestBoundaryNode1, DestBoundaryNode2).first;
                    #ifdef _LOGTIME1_
                    auto t313_end = std::chrono::steady_clock::now();
                    std::cout << ", t313: " << std::chrono::duration<double>(t313_end - t313_start).count();
                    #endif

                    #ifdef _LOGTIME1_
                    auto t314_start = std::chrono::steady_clock::now();
                    #endif                    
                    Ipv4Address DestBoundaryIP = GetIpFromInterface(DestBoundaryNode1, interface);                   
                    #ifdef _LOGTIME1_
                    auto t314_end = std::chrono::steady_clock::now();
                    std::cout << ", t314: " << std::chrono::duration<double>(t314_end - t314_start).count();
                    #endif

                    #ifdef _LOGTIME1_
                    auto t315_start = std::chrono::steady_clock::now();
                    #endif                    
                    if(m_deviceU.count(DestBoundaryIP) != 0)    DestBoundaryU = m_deviceU[DestBoundaryIP];
                    #ifdef _LOGTIME1_
                    auto t315_end = std::chrono::steady_clock::now();
                    std::cout << ", t315: " << std::chrono::duration<double>(t315_end - t315_start).count() << std::endl;
                    #endif                                                   
                    #ifdef _LOGROU_
                    std::cout << "|Boundary Link Metric|: " << DestBoundaryU << std::endl;
                    #endif
                    #ifdef _LOGTIME_
                    auto t31_end = std::chrono::steady_clock::now();
                    std::cout << "t31: " << std::chrono::duration<double>(t31_end - t31_start).count();
                    #endif

                    uint32_t boundSatID1 = DestBoundaryNode1->GetId() - index;
                    // 判断边界节点是否在矩形域内（consType 0: Walker Star, consType 1: Walker Delta）
                    #ifdef _LOGTIME_
                    auto t32_start = std::chrono::steady_clock::now();
                    #endif
                    if (!consType) regionFlag1 = InRegionforWalkerStar (curSatID, dstSatID, boundSatID1, numOfOrbit, satPerOrbit);
                    else regionFlag1 = InRegionforWalkerDelta (curSatID, dstSatID, boundSatID1, numOfOrbit, satPerOrbit);
                    if(regionFlag1)
                    {// 选取矩形域内，链路带宽利用率更小的边界节点
                        inRegionCnt++;
                        if(DestBoundaryU <= DestBoundaryUtmp)
                        {
                            DestBoundaryNode = DestBoundaryNode1;
                            DestBoundaryUtmp = DestBoundaryU;
                        }
                    }
                    #ifdef _LOGTIME_
                    auto t32_end = std::chrono::steady_clock::now();
                    std::cout << ", t32: " << std::chrono::duration<double>(t32_end - t32_start).count();
                    #endif                    

                    #ifdef _LOGROU_                    
                    std::cout << "BoundaryNode 1 为本簇节点" << ", ID号为：" << DestBoundaryNode1->GetId() << "\n"; 
                    std::cout << "regionFlag1: " << regionFlag1 << std::endl;
                    #endif   
                    
                    #ifdef rouLinkMin
                    if(DestBoundaryU <= DestBoundaryUtotal) 
                    {// 如果所有边界节点都不在矩形区域内，仅选取链路带宽利用率更小的边界节点
                        DestBoundaryNodeTotal = DestBoundaryNode1;
                        DestBoundaryUtotal = DestBoundaryU;
                    }

                    if((a == (bSize - 1)) && (inRegionCnt == 0))
                        DestBoundaryNode = DestBoundaryNodeTotal;
                    #endif  
                }
                #ifdef _LOGTIME_
                auto t3_end = std::chrono::steady_clock::now();
                std::cout << ", \t t3: " << std::chrono::duration<double>(t3_end - t3_start).count() << std::endl;
                #endif

                #ifdef _LOGTIME_
                auto t4_start = std::chrono::steady_clock::now();
                #endif
                if(DestBoundaryNode != nullptr)
                {
                    #ifdef _LOGROU_
                    std::cout << "CurNode ID : " << cluster.Get(j)->GetId() << "          curClusterNum: " << ClusterNum << std::endl;
                    std::cout << "BoundaryNode ID : " << DestBoundaryNode->GetId() << "     nxtClusterNum: " << NextClusterNum << std::endl;
                    std::cout << "DestNode ID : " << node->GetId() << "         dstClusterNum: " << node->m_ClusterNumber << std::endl;
                    std::cout << "DestBoundaryNode Num : " << BoundaryNode[ClusterNum][NextClusterNum].size() << std::endl;
                    #endif
                    
                    // 获取主路径
                    Ipv4RoutingTableEntry* routerentry = GetSatNextFromTable(cluster.Get(j), DestBoundaryNode);
                    if (routerentry == nullptr)    continue;
                    Ipv4Address NextHopAddress = routerentry->GetGateway();
                    int32_t NextHopInterface = routerentry->GetInterface();
                    #ifdef _LOGTIME_
                    auto t41_end = std::chrono::steady_clock::now();
                    #endif
                    // 获取备份路径
                    int32_t NextHopBackUpInterface = GetBackupInterface(cluster.Get(j), node, NextHopInterface, index, 
                                                                        consType, numOfOrbit, satPerOrbit);                
                    #ifdef _LOGTIME_
                    auto t42_end = std::chrono::steady_clock::now();
                    #endif
                    Ptr<GlobalRouter> router = cluster.Get(j)->GetObject<GlobalRouter>();
                    Ptr<Ipv4GlobalRouting> gr = router->GetRoutingProtocol();
                    for(uint8_t n = 1; n < NumInterface; ++n)
                    {
                        // 添加主路由表项 
                        gr->AddHostRouteTo(GetIPAddress(node, (uint32_t)n), NextHopAddress, NextHopInterface);
                        #ifdef _LOGTIME_
                        auto t43_end = std::chrono::steady_clock::now();
                        std::cout << "t43: " << std::chrono::duration<double>(t43_end - t42_end).count();
                        #endif
                        if (NextHopBackUpInterface > 0)
                        {// 添加备份路由表项
                            Ipv4Address NextHopBackUpAddress = GetNeiIpFromInterface(cluster.Get(j), NextHopBackUpInterface);                            
                            gr->AddHostRouteTo(GetIPAddress(node, (uint32_t)n), NextHopBackUpAddress, NextHopBackUpInterface);    
                        }
                        #ifdef _LOGTIME_
                        auto t44_end = std::chrono::steady_clock::now();
                        std::cout << ", t44: " << std::chrono::duration<double>(t44_end - t43_end).count();
                        #endif
                    }
                }
                #ifdef _LOGTIME_
                auto t4_end = std::chrono::steady_clock::now();
                
                std::cout << ", t41: " << std::chrono::duration<double>(t41_end - t4_start).count() << \
                        ", t42: " << std::chrono::duration<double>(t42_end - t41_end).count();
                std::cout << ", \tt4: " << std::chrono::duration<double>(t4_end - t4_start).count() << std::endl;             
                #endif                

                #ifndef rouLinkMin
                // if((a == (bSize - 1)) && (inRegionCnt == 0))
                if(DestBoundaryNode == nullptr)
                {// 如果没有边界节点在矩形域内，选取矩形域内的邻居转发
                    for(uint32_t k = 0; k < neighbor.GetN(); ++k)
                    {
                        bool inRegion = false;
                        if(!consType)
                            inRegion = InRegionforWalkerStar(curSatID, i, neighbor.Get(k)->GetId()-index, numOfOrbit, satPerOrbit);
                        else
                            inRegion = InRegionforWalkerDelta(curSatID, i, neighbor.Get(k)->GetId()-index, numOfOrbit, satPerOrbit);
                        
                        if(inRegion)
                        {
                            Ptr<GlobalRouter> router = cluster.Get(j)->GetObject<GlobalRouter>();
                            Ptr<Ipv4GlobalRouting> gr = router->GetRoutingProtocol();
                            int32_t interface1 = GetInterface(cluster.Get(j), neighbor.Get(k)).first;
                            int32_t interface2 = GetInterface(cluster.Get(j), neighbor.Get(k)).second;
                            
                            int32_t backupInterface1;
                            for(uint8_t m = 0; m < NumInterface; m++)
                            {
                                if((m != interface1) && (m != interface2))
                                {// 选取矩形域邻居作为备份路径
                                    backupInterface1 = m;
                                    break;
                                }
                            }

                            for(uint8_t n = 1; n < NumInterface; ++n)
                            {// 添加路由表项
                                // 添加主路由表项
                                gr->AddHostRouteTo(GetIPAddress(node, (uint32_t)n), 
                                                                GetIpFromInterface(neighbor.Get(k), interface2), interface1);
                                if (backupInterface1 > 0)
                                {// 添加备份路由表项
                                    Ipv4Address NextHopBackUpAddress = GetNeiIpFromInterface(cluster.Get(j), backupInterface1);                            
                                    gr->AddHostRouteTo(GetIPAddress(node, (uint32_t)n), NextHopBackUpAddress, backupInterface1);    
                                }
                            }

                            break;
                        }
                    }

                }
                #endif

                #ifdef _LOGROU_                    
                // std::cout << "簇间主路由：" << std::endl;
                // std::cout << "DstNodeAddress: " << GetIPAddress(node, 1) << std:: endl;
                // std::cout << "NextHopAddress: " << NextHopAddress << std::endl;
                // std::cout << "NextHopInterface: " << NextHopInterface << std::endl;
                // std::cout << "簇间备份路由：" << std::endl;
                // std::cout << "DstNodeAddress: " << GetIPAddress(node, 1) << std:: endl;
                // std::cout << "NextHopBackUpAddress: " << NextHopBackUpAddress << std::endl;
                // std::cout << "NextHopBackUpInterface: " << NextHopBackUpInterface << std::endl << std::endl;
                #endif
            }
        }
    }
}

void
SatGlobalRouteManagerImpl::ClusteringAddBackupRoutes(NodeContainer& cluster,
                                                     NodeContainer& GlobalNode,
                                                     bool consType,
                                                     uint32_t numOfOrbit, 
                                                     uint32_t satPerOrbit)
{
    // 初始化参数
    Ptr<Node> curNode;                              // 当前节点
    Ptr<Node> dstNode;                              // 簇内目的节点
    uint8_t NumInterface = 0;                       // 目的节点端口数
    uint8_t index = GlobalNode.Get(0)->GetId();     // 由于地面站存在，获取卫星起始ID

    for(uint32_t j = 0; j < cluster.GetN(); ++j)
    {
        for(uint32_t k = 0; k < cluster.GetN(); k++)
        {// 进行全局节点的遍历，寻找不是本簇的节点    
            if(j == k)  continue;
            curNode = cluster.Get(j);
            dstNode = cluster.Get(k);
            // 获取主路径
            Ipv4RoutingTableEntry* routerentry = GetSatNextFromTable(curNode, dstNode);
            if(routerentry != nullptr)
            {
                int32_t NextHopInterface = routerentry->GetInterface();
                // 获取备份路径
                int32_t NextHopBackUpInterface = GetBackupInterface(curNode, dstNode, NextHopInterface, index, 
                                                                    consType, numOfOrbit, satPerOrbit);                
                Ptr<GlobalRouter> router = curNode->GetObject<GlobalRouter>();
                Ptr<Ipv4GlobalRouting> gr = router->GetRoutingProtocol();
                NumInterface = (uint8_t)dstNode->GetObject<Ipv4>()->GetNInterfaces();
                for(uint8_t i = 1; i < NumInterface; ++i)
                {
                    if (NextHopBackUpInterface > 0)
                    {// 添加备份路由表项
                        Ipv4Address NextHopBackUpAddress = GetNeiIpFromInterface(curNode, NextHopBackUpInterface);                            
                        gr->AddHostRouteTo(GetIPAddress(dstNode, (uint32_t)i), NextHopBackUpAddress, NextHopBackUpInterface);    
                    }
                }
            }
            else
            {
                continue;   // !<假设无主路由路径，后续考虑备份路由方案
            }
        }
    }
}
//
// This method is derived from quagga ospf_spf_next ().  See RFC2328 Section 
// 16.1 (2) for further details.
//
// We're passed a parameter <v> that is a vertex which is already in the SPF
// tree.  A vertex represents a router node.  We also get a reference to the
// SPF candidate queue, which is a priority queue containing the shortest paths
// to the networks we know about.
//
// We examine the links in v's LSA and update the list of candidates with any
// vertices not already on the list.  If a lower-cost path is found to a
// vertex already on the candidate list, store the new (lower) cost.
//
void
SatGlobalRouteManagerImpl::SPFNext (SATSPFVertex* v, SatCandidateQueue& candidate)
{
  NS_LOG_FUNCTION (this << v << &candidate);
//   std::cout << "SPFNext" << "test1" << std::endl;
  SATSPFVertex* w = 0;
  GlobalRoutingLSA* w_lsa = 0;
  GlobalRoutingLinkRecord *l = 0;
  double distance = 0;
  uint32_t numRecordsInVertex = 0;
//
// V points to a Router-LSA or Network-LSA
// Loop over the links in router LSA or attached routers in Network LSA
//
    if (v->GetVertexType () == SATSPFVertex::VertexRouter)
    {
      numRecordsInVertex = v->GetLSA ()->GetNLinkRecords (); 
    }
    if (v->GetVertexType () == SATSPFVertex::VertexNetwork)
    {
      numRecordsInVertex = v->GetLSA ()->GetNAttachedRouters (); 
    }
    // std::cout << "SPFNext" << "test2" << std::endl;


    for (uint32_t i = 0; i < numRecordsInVertex; i++)
    {
    // Get w_lsa:  In case of V is Router-LSA
        // std::cout << "SPFNext" << "test31" << std::endl;
        if (v->GetVertexType () == SATSPFVertex::VertexRouter) 
        {
            NS_LOG_LOGIC ("Examining link " << i << " of " << 
                            v->GetVertexId () << "'s " <<
                            v->GetLSA ()->GetNLinkRecords () << " link records");

            // std::cout << "Examining link " << i << " of " << 
            //                 v->GetVertexId () << "'s " <<
            //                 v->GetLSA ()->GetNLinkRecords () << " link records" << std::endl;
//
// (a) If this is a link to a stub network, examine the next link in V's LSA.
// Links to stub networks will be considered in the second stage of the
// shortest path calculation.
//
            l = v->GetLSA ()->GetLinkRecord (i);
            
///------------------- Get link weight and change it -------------------///
            
                if (l->GetLinkType () == GlobalRoutingLinkRecord::PointToPoint)
                {
                    
                    double mymetric = 20.0;
                    // 通过key找value 
                    #ifdef _INTRAROU1_
                    Ipv4Address addresstmp = l->GetLinkData();
                    if(m_deviceU.count(addresstmp) > 0)
                    {                        
                        /* Weight1 */
                        if((m_deviceU[addresstmp] >= 1e-6) || m_deviceU[addresstmp] <= -1e-6)
                            mymetric = std::pow(15, m_deviceU[addresstmp]);
                    }
                    #endif
                    #ifdef _INTRAROU2_
                    Ipv4Address addresstmp = l->GetLinkData();
                    /* Weight2 */
                    uint8_t buf1[4];
                    addresstmp.Serialize(buf1);
                    Ptr<Node> node = NodeList::GetNode(6 + (buf1[1]-4)*SatPerOrbit + buf1[2]); 
                    // std::cout << "NodeID: " << node->GetId() << std::endl;
                    for(uint32_t j = 0; j < node->GetNDevices(); j++)
                    {                       
                        Ptr<PointToPointNetDevice> p2pNetDevice = DynamicCast<PointToPointNetDevice>(node->GetDevice(j));
                        Ptr<Ipv4> ippp = node->GetObject<Ipv4> ();
                        if(ippp->GetNAddresses(j) == 0) continue;
                        Ipv4Address ip = ippp->GetAddress(j, 0).GetLocal();
                        if((p2pNetDevice != nullptr) && (ip == addresstmp))
                        {
                            Ptr<Queue<Packet>> dQueue = p2pNetDevice->GetQueue();
                            // std::cout << "queue: " << (double)dQueue->GetNPackets() << std::endl;
                            mymetric = std::pow(15, ((double)dQueue->GetNPackets() / 1000.0));
                        }
                    }
                    #endif
                    
                    #ifdef _INTRAROUSIM_
                    Ipv4Address addresstmp = l->GetLinkData();
                    /* WeightSIM */
                    uint8_t buf1[4];
                    addresstmp.Serialize(buf1);
                    Ptr<Node> node = NodeList::GetNode(Indexsat - 1 + (buf1[1]-4)*SatPerOrbit + buf1[2]); 
                    // std::cout << "NodeID: " << node->GetId() << std::endl;
                    for(uint32_t j = 0; j < node->GetNDevices(); j++)
                    {                       
                      Ptr<PointToPointNetDevice> p2pNetDevice = DynamicCast<PointToPointNetDevice>(node->GetDevice(j));
                      
                      Ptr<Ipv4> ippp = node->GetObject<Ipv4> ();
                      if(ippp->GetNAddresses(j) == 0) continue;
                      Ipv4Address ip = ippp->GetAddress(j, 0).GetLocal();
                      if((p2pNetDevice != nullptr) && (ip == addresstmp))
                      {
                        DataRateValue dataLoadValue, dataRateValue;
                        p2pNetDevice->GetAttribute("DataLoad", dataLoadValue);
                        p2pNetDevice->GetAttribute("DataRate", dataRateValue);
                        DataRate dataLoad = dataLoadValue.Get();
                        uint32_t dataLoadValueUint = dataLoad.GetBitRate() / 1000; // 转换为 uint32_t, 单位为Kbps
                        DataRate dataRate = dataRateValue.Get();
                        uint32_t dataRateValueUint = dataRate.GetBitRate() / 1000; // 转换为 uint32_t, 单位为Kbps
                        double current_utilization = ((double)dataLoadValueUint / (double)dataRateValueUint);
                        
                        Ptr<PointToPointChannel> channel1 = DynamicCast<PointToPointChannel>(p2pNetDevice->GetChannel());
                        Time delay = channel1->GetDelay();
                        int64_t delayValue = delay.GetMicroSeconds();
                        mymetric = std::pow(delayValue, current_utilization);
                      }
                    }
                    #endif
                    // std::cout << "|Intra Cluster| \n  Metric: " << l->GetMetric() << std::endl;
                    // std::cout << "  Addresstmp: " << addresstmp << ", mymetric: " << mymetric << std::endl;
                    l->SetMetric(mymetric);
                    // std::cout << "  Changed Metric: " << l->GetMetric() << std::endl;
                    // l->SetMetric(1.0);
                }   
///------------------- Get link weight and change it -------------------///

            NS_ASSERT (l != 0);
            if (l->GetLinkType () == GlobalRoutingLinkRecord::StubNetwork)
            {
              NS_LOG_LOGIC ("Found a Stub record to " << l->GetLinkId ());
              continue;
            }

//
// (b) Otherwise, W is a transit vertex (router or transit network).  Look up
// the vertex W's LSA (router-LSA or network-LSA) in Area A's link state
// database. 
//
            if (l->GetLinkType () == GlobalRoutingLinkRecord::PointToPoint)
            {
//
// Lookup the link state advertisement of the new link -- we call it <w> in
// the link state database.
//

                w_lsa = m_lsdb->GetLSA (l->GetLinkId ());

                if(!w_lsa) continue;
                NS_ASSERT (w_lsa);

                NS_LOG_LOGIC ("Found a P2P record from " << 
                                v->GetVertexId () << " to " << w_lsa->GetLinkStateId ());
            }
            else if (l->GetLinkType () == 
                   GlobalRoutingLinkRecord::TransitNetwork)
            {
              w_lsa = m_lsdb->GetLSA (l->GetLinkId ());
              NS_ASSERT (w_lsa);
              NS_LOG_LOGIC ("Found a Transit record from " << 
                            v->GetVertexId () << " to " << w_lsa->GetLinkStateId ());
            }
            else 
            {
              NS_ASSERT_MSG (0, "illegal Link Type");
            }

        }
// Get w_lsa:  In case of V is Network-LSA
        if (v->GetVertexType () == SATSPFVertex::VertexNetwork) 
        {
          w_lsa = m_lsdb->GetLSAByLinkData 
              (v->GetLSA ()->GetAttachedRouter (i));
          if (!w_lsa)
            {
              continue;
            }
          NS_LOG_LOGIC ("Found a Network LSA from " << 
                        v->GetVertexId () << " to " << w_lsa->GetLinkStateId ());
        }


// Note:  w_lsa at this point may be either RouterLSA or NetworkLSA
//
// (c) If vertex W is already on the shortest-path tree, examine the next
// link in the LSA.
//
// If the link is to a router that is already in the shortest path first tree
// then we have it covered -- ignore it.
//
        if (w_lsa->GetStatus () == GlobalRoutingLSA::LSA_SPF_IN_SPFTREE) 
        {
          NS_LOG_LOGIC ("Skipping ->  LSA "<< 
                        w_lsa->GetLinkStateId () << " already in SPF tree");
          continue;
        }

//
// (d) Calculate the link state cost D of the resulting path from the root to 
// vertex W.  D is equal to the sum of the link state cost of the (already 
// calculated) shortest path to vertex V and the advertised cost of the link
// between vertices V and W.
//
        if (v->GetLSA ()->GetLSType () == GlobalRoutingLSA::RouterLSA)
        {
          NS_ASSERT (l != 0);
          distance = v->GetDistanceFromRoot () + l->GetMetric ();
        }
        else
        {
          distance = v->GetDistanceFromRoot ();
        }


        NS_LOG_LOGIC ("Considering w_lsa " << w_lsa->GetLinkStateId ());
        

// Is there already vertex w in candidate list?
        if (w_lsa->GetStatus () == GlobalRoutingLSA::LSA_SPF_NOT_EXPLORED)
        {
// Calculate nexthop to w
// We need to figure out how to actually get to the new router represented
// by <w>.  This will (among other things) find the next hop address to send
// packets destined for this network to, and also find the outbound interface
// used to forward the packets.

// prepare vertex w
            w = new SATSPFVertex (w_lsa);
            if (SPFNexthopCalculation (v, w, l, distance))
            {
                w_lsa->SetStatus (GlobalRoutingLSA::LSA_SPF_CANDIDATE);
//
// Push this new vertex onto the priority queue (ordered by distance from the
// root node).
//
                candidate.Push (w);
                NS_LOG_LOGIC ("Pushing " << 
                            w->GetVertexId () << ", parent vertexId: " <<
                            v->GetVertexId () << ", distance: " <<
                            w->GetDistanceFromRoot ());
            }
            else
                NS_ASSERT_MSG (0, "SPFNexthopCalculation never " 
                           << "return false, but it does now!");
        }
        else if (w_lsa->GetStatus () == GlobalRoutingLSA::LSA_SPF_CANDIDATE)
        {
//
// We have already considered the link represented by <w>.  What wse have to
// do now is to decide if this new router represents a route with a shorter
// distance metric.
//
// So, locate the vertex in the candidate queue and take a look at the 
// distance.

/* (quagga-0.98.6) W is already on the candidate list; call it cw.
* Compare the previously calculated cost (cw->distance)
* with the cost we just determined (w->distance) to see
* if we've found a shorter path.
*/
          SATSPFVertex* cw;
          cw = candidate.Find (w_lsa->GetLinkStateId ());
          if (cw->GetDistanceFromRoot () < distance)
            {
//
// This is not a shorter path, so don't do anything.
//
              continue;
            }
          else if (cw->GetDistanceFromRoot () == distance)
            {
//
// This path is one with an equal cost.
//
              NS_LOG_LOGIC ("Equal cost multiple paths found.");

// At this point, there are two instances 'w' and 'cw' of the
// same vertex, the vertex that is currently being considered
// for adding into the shortest path tree. 'w' is the instance
// as seen from the root via vertex 'v', and 'cw' is the instance 
// as seen from the root via some other vertices other than 'v'.
// These two instances are being merged in the following code.
// In particular, the parent nodes, the next hops, and the root's
// output interfaces of the two instances are being merged.
// 
// Note that this is functionally equivalent to calling
// ospf_nexthop_merge (cw->nexthop, w->nexthop) in quagga-0.98.6
// (ospf_spf.c::859), although the detail implementation
// is very different from quagga (blame ns3::GlobalRouteManagerImpl)

// prepare vertex w
              w = new SATSPFVertex (w_lsa);
              SPFNexthopCalculation (v, w, l, distance);
              cw->MergeRootExitDirections (w);
              cw->MergeParent (w);
// SPFVertexAddParent (w) is necessary as the destructor of 
// SPFVertex checks if the vertex and its parent is linked
// bidirectionally
              SPFVertexAddParent (w);
              delete w;
            }
            else // cw->GetDistanceFromRoot () > w->GetDistanceFromRoot ()
            {
// 
// this path represents a new, lower-cost path to <w> (the vertex we found in
// the current link record of the link state advertisement of the current root
// (vertex <v>)
//
// N.B. the nexthop_calculation is conditional, if it finds a valid nexthop
// it will call spf_add_parents, which will flush the old parents
//
                if (SPFNexthopCalculation (v, cw, l, distance))
                {
//
// If we've changed the cost to get to the vertex represented by <w>, we 
// must reorder the priority queue keyed to that cost.
//
                  candidate.Reorder ();
                }
            } // new lower cost path found
        } // end W is already on the candidate list
    } // end loop over the links in V's LSA
}

void
SatGlobalRouteManagerImpl::SPFNextforClusters (SATSPFVertex* v, SatCandidateQueue& candidate, std::vector<std::vector<std::vector<std::pair<uint32_t, uint32_t>>>> BoundaryNode)
{
    NS_LOG_FUNCTION (this << v << &candidate);

    SATSPFVertex* w = 0;
    GlobalRoutingLSA* w_lsa = 0;
    GlobalRoutingLinkRecord *l = 0;
    double distance = 0;
    uint32_t numRecordsInVertex = 0;

    // V points to a Router-LSA or Network-LSA
    // Loop over the links in router LSA or attached routers in Network LSA
    if (v->GetVertexType () == SATSPFVertex::VertexRouter)
    {
        numRecordsInVertex = v->GetLSA ()->GetNLinkRecords (); 
    }
    if (v->GetVertexType () == SATSPFVertex::VertexNetwork)
    {
        numRecordsInVertex = v->GetLSA ()->GetNAttachedRouters (); 
    }
    // std::cout << "SPFNextforClusters test3: " << numRecordsInVertex << std::endl;


    for (uint32_t i = 0; i < numRecordsInVertex; i++)
    {
        // Get w_lsa:  In case of V is Router-LSA
        // std::cout << "SPFNEXTFORCLUSTER test2" << std::endl;
        if (v->GetVertexType () == SATSPFVertex::VertexRouter) 
        {
            NS_LOG_LOGIC ("Examining link " << i << " of " << v->GetVertexId () << "'s " <<
                            v->GetLSA ()->GetNLinkRecords () << " link records");



            l = v->GetLSA ()->GetLinkRecord (i);


            NS_ASSERT (l != 0);
            if (l->GetLinkType () == GlobalRoutingLinkRecord::StubNetwork)
            {// (a) If this is a link to a stub network, examine the next link in V's LSA.
                // Links to stub networks will be considered in the second stage of the shortest path calculation.
                NS_LOG_LOGIC ("Found a Stub record to " << l->GetLinkId ());
                continue;
            }

            // std::cout << "SPFNEXTFORCLUSTER test1" << std::endl;
            if (l->GetLinkType () == GlobalRoutingLinkRecord::PointToPoint)
            {// (b) Otherwise, W is a transit vertex (router or transit network).  
                // Look up the vertex W's LSA (router-LSA or network-LSA) in Area A's link state database. 

                w_lsa = m_lsdb->GetLSA (l->GetLinkId ());

                if(!w_lsa) continue;
                NS_ASSERT (w_lsa);

                NS_LOG_LOGIC ("Found a P2P record from " << 
                                v->GetVertexId () << " to " << w_lsa->GetLinkStateId ());

                #ifdef _INTERROU_
                ///------------------- Get link weight and change it -------------------///
                double linkMetric = 1.0;

                Ipv4Address curIP = v->GetVertexId ();
                Ipv4Address neiIP = w_lsa->GetLinkStateId ();
                uint8_t buf1[4], buf2[4];
                curIP.Serialize(buf1);
                neiIP.Serialize(buf2);

                // uint32_t curClusterID = uint32_t(buf1[3]) - NumOfOrbit*SatPerOrbit - Indexsat;          // !< 待调试
                // uint32_t neiClusterID = uint32_t(buf2[3]) - NumOfOrbit*SatPerOrbit - Indexsat;          // !< 待调试
                // linkMetric = ComputeClusterLBU(curClusterID, neiClusterID, BoundaryNode);               // !< 待调试

                Ipv4Address addresstmp = l->GetLinkData();

                // std::cout << "Print Cluster address: " << addresstmp << std::endl;
                // 通过key找value 
                if(m_clusterCost.count(addresstmp) > 0)
                {
                    
                    double mymetric = 1000.0;
                    if (m_clusterCost[addresstmp] >= 1e-6)
                    {
                        mymetric = linkMetric + m_clusterCost[addresstmp] / m_maxClusterDiameter;
                        // std::cout << "|Inter Cluster| \n  Metric: " << mymetric << std::endl;
                    }

                    // std::cout << "|Inter Cluster| \n  Metric: " << l->GetMetric() << std::endl;
                    l->SetMetric(mymetric);
                    // std::cout << "  Changed Metric: " << l->GetMetric() << std::endl;
                }
                // std::cout << "Change inter metrix finished " << std::endl;
                ///------------------- Get link weight and change it -------------------///
                #endif
            }
            else if (l->GetLinkType () == GlobalRoutingLinkRecord::TransitNetwork)
            {
                w_lsa = m_lsdb->GetLSA (l->GetLinkId ());
                NS_ASSERT (w_lsa);
                NS_LOG_LOGIC ("Found a Transit record from " << 
                            v->GetVertexId () << " to " << w_lsa->GetLinkStateId ());
            }
            else 
            {
                NS_ASSERT_MSG (0, "illegal Link Type");
            }

        }
        
        // Get w_lsa:  In case of V is Network-LSA
        if (v->GetVertexType () == SATSPFVertex::VertexNetwork) 
        {
            w_lsa = m_lsdb->GetLSAByLinkData (v->GetLSA ()->GetAttachedRouter (i));
            if (!w_lsa)
            {
                continue;
            }
            NS_LOG_LOGIC ("Found a Network LSA from " << v->GetVertexId () << " to " << w_lsa->GetLinkStateId ());
        }


        // Note:  w_lsa at this point may be either RouterLSA or NetworkLSA

        // (c) If vertex W is already on the shortest-path tree, examine the next
        // link in the LSA.
        if (w_lsa->GetStatus () == GlobalRoutingLSA::LSA_SPF_IN_SPFTREE) 
        {
            NS_LOG_LOGIC ("Skipping ->  LSA "<< w_lsa->GetLinkStateId () << " already in SPF tree");
            continue;
        }

        // (d) Calculate the link state cost D of the resulting path from the root to vertex W.
        if (v->GetLSA ()->GetLSType () == GlobalRoutingLSA::RouterLSA)
        {
            NS_ASSERT (l != 0);
            distance = v->GetDistanceFromRoot () + l->GetMetric ();
        }
        else
        {
            distance = v->GetDistanceFromRoot ();
        }

        NS_LOG_LOGIC ("Considering w_lsa " << w_lsa->GetLinkStateId ());
        

        // Is there already vertex w in candidate list?
        if (w_lsa->GetStatus () == GlobalRoutingLSA::LSA_SPF_NOT_EXPLORED)
        {
            // Calculate nexthop to w
            // We need to figure out how to actually get to the new router represented by <w>.  
            // This will find the next hop address to send packets destined for this network to
            // and also find the outbound interface used to forward the packets.

            // prepare vertex w
            w = new SATSPFVertex (w_lsa);
            if (SPFNexthopCalculation (v, w, l, distance))
            {
                w_lsa->SetStatus (GlobalRoutingLSA::LSA_SPF_CANDIDATE);

                // Push this new vertex onto the priority queue (ordered by distance from the root node).
                candidate.Push (w);
                NS_LOG_LOGIC ("Pushing " << 
                            w->GetVertexId () << ", parent vertexId: " <<
                            v->GetVertexId () << ", distance: " <<
                            w->GetDistanceFromRoot ());
            }
            else
                NS_ASSERT_MSG (0, "SPFNexthopCalculation never " 
                           << "return false, but it does now!");
        }
        else if (w_lsa->GetStatus () == GlobalRoutingLSA::LSA_SPF_CANDIDATE)
        {
            // We have already considered the link represented by <w>.  What wse have to
            // do now is to decide if this new router represents a route with a shorter distance metric.
            // So, locate the vertex in the candidate queue and take a look at the distance.

            /* (quagga-0.98.6) W is already on the candidate list; call it cw.
            * Compare the previously calculated cost (cw->distance)
            * with the cost we just determined (w->distance) to see
            * if we've found a shorter path.
            */
            SATSPFVertex* cw;
            cw = candidate.Find (w_lsa->GetLinkStateId ());
            if (cw->GetDistanceFromRoot () < distance)
            {// This is not a shorter path, so don't do anything.
                continue;
            }
            else if (cw->GetDistanceFromRoot () == distance)
            {// This path is one with an equal cost.
                NS_LOG_LOGIC ("Equal cost multiple paths found.");

                // At this point, there are two instances 'w' and 'cw' of the
                // same vertex, the vertex that is currently being considered
                // for adding into the shortest path tree. 'w' is the instance
                // as seen from the root via vertex 'v', and 'cw' is the instance 
                // as seen from the root via some other vertices other than 'v'.
                // These two instances are being merged in the following code.
                // In particular, the parent nodes, the next hops, and the root's
                // output interfaces of the two instances are being merged.

                // Note that this is functionally equivalent to calling
                // ospf_nexthop_merge (cw->nexthop, w->nexthop) in quagga-0.98.6
                // (ospf_spf.c::859), although the detail implementation
                // is very different from quagga (blame ns3::GlobalRouteManagerImpl)

                // prepare vertex w
                w = new SATSPFVertex (w_lsa);
                SPFNexthopCalculation (v, w, l, distance);
                cw->MergeRootExitDirections (w);
                cw->MergeParent (w);
                // SPFVertexAddParent (w) is necessary as the destructor of 
                // SPFVertex checks if the vertex and its parent is linked bidirectionally
                SPFVertexAddParent (w);
                delete w;
            }
            else // cw->GetDistanceFromRoot () > w->GetDistanceFromRoot ()
            {
                // this path represents a new, lower-cost path to <w>

                if (SPFNexthopCalculation (v, cw, l, distance))
                {
                    //
                    // If we've changed the cost to get to the vertex represented by <w>, we 
                    // must reorder the priority queue keyed to that cost.
                    //
                    candidate.Reorder ();
                }
            } // new lower cost path found
        } // end W is already on the candidate list
    } // end loop over the links in V's LSA
}


//
// This method is derived from quagga ospf_nexthop_calculation() 16.1.1.
//
// Calculate nexthop from root through V (parent) to vertex W (destination)
// with given distance from root->W.
//
// As appropriate, set w's parent, distance, and nexthop information
//
// For now, this is greatly simplified from the quagga code
//
int
SatGlobalRouteManagerImpl::SPFNexthopCalculation (
  SATSPFVertex* v, 
  SATSPFVertex* w,
  GlobalRoutingLinkRecord* l,
  uint32_t distance)
{
  NS_LOG_FUNCTION (this << v << w << l << distance);
//
// If w is a NetworkVertex, l should be null
/*
  if (w->GetVertexType () == SPFVertex::VertexNetwork && l)
    {
        NS_ASSERT_MSG (0, "Error:  SPFNexthopCalculation parameter problem");
    }
*/

//
// The vertex m_spfroot is a distinguished vertex representing the node at
// the root of the calculations.  That is, it is the node for which we are
// calculating the routes.
//
// There are two distinct cases for calculating the next hop information.
// First, if we're considering a hop from the root to an "adjacent" network
// (one that is on the other side of a point-to-point link connected to the
// root), then we need to store the information needed to forward down that
// link.  The second case is if the network is not directly adjacent.  In that
// case we need to use the forwarding information from the vertex on the path
// to the destination that is directly adjacent [node 1] in both cases of the
// diagram below.
// 
// (1) [root] -> [point-to-point] -> [node 1]
// (2) [root] -> [point-to-point] -> [node 1] -> [point-to-point] -> [node 2]
//
// We call the propagation of next hop information down vertices of a path
// "inheriting" the next hop information.
//
// The point-to-point link information is only useful in this calculation when
// we are examining the root node. 
//
  if (v == m_spfroot)
    {
//
// In this case <v> is the root node, which means it is the starting point
// for the packets forwarded by that node.  This also means that the next hop
// address of packets headed for some arbitrary off-network destination must
// be the destination at the other end of one of the links off of the root
// node if this root node is a router.  We then need to see if this node <w>
// is a router.
//
      if (w->GetVertexType () == SATSPFVertex::VertexRouter) 
        {
//
// In the case of point-to-point links, the link data field (m_linkData) of a
// Global Router Link Record contains the local IP address.  If we look at the
// link record describing the link from the perspecive of <w> (the remote
// node from the viewpoint of <v>) back to the root node, we can discover the
// IP address of the router to which <v> is adjacent.  This is a distinguished
// address -- the next hop address to get from <v> to <w> and all networks 
// accessed through that path.
//
// SPFGetNextLink () is a little odd.  used in this way it is just going to
// return the link record describing the link from <w> to <v>.  Think of it as
// SPFGetLink.
//
          NS_ASSERT (l);
          GlobalRoutingLinkRecord *linkRemote = 0;
          linkRemote = SPFGetNextLink (w, v, linkRemote);
// 
// At this point, <l> is the Global Router Link Record describing the point-
// to point link from <v> to <w> from the perspective of <v>; and <linkRemote>
// is the Global Router Link Record describing that same link from the 
// perspective of <w> (back to <v>).  Now we can just copy the next hop 
// address from the m_linkData member variable.
// 
// The next hop member variable we put in <w> has the sense "in order to get
// from the root node to the host represented by vertex <w>, you have to send
// the packet to the next hop address specified in w->m_nextHop.
//
          Ipv4Address nextHop = linkRemote->GetLinkData ();
// 
// Now find the outgoing interface corresponding to the point to point link
// from the perspective of <v> -- remember that <l> is the link "from"
// <v> "to" <w>.
//
          uint32_t outIf = FindOutgoingInterfaceId (l->GetLinkData ());

          w->SetRootExitDirection (nextHop, outIf);
          w->SetDistanceFromRoot (distance);
          w->SetParent (v);
          NS_LOG_LOGIC ("Next hop from " << 
                        v->GetVertexId () << " to " << w->GetVertexId () <<
                        " goes through next hop " << nextHop <<
                        " via outgoing interface " << outIf <<
                        " with distance " << distance);
        }  // end W is a router vertes
      else 
        {
          NS_ASSERT (w->GetVertexType () == SATSPFVertex::VertexNetwork);
// W is a directly connected network; no next hop is required
          GlobalRoutingLSA* w_lsa = w->GetLSA ();
          NS_ASSERT (w_lsa->GetLSType () == GlobalRoutingLSA::NetworkLSA);
// Find outgoing interface ID for this network
          uint32_t outIf = FindOutgoingInterfaceId (w_lsa->GetLinkStateId (), 
                                                    w_lsa->GetNetworkLSANetworkMask () );
// Set the next hop to 0.0.0.0 meaning "not exist"
          Ipv4Address nextHop = Ipv4Address::GetZero ();
          w->SetRootExitDirection (nextHop, outIf);
          w->SetDistanceFromRoot (distance);
          w->SetParent (v);
          NS_LOG_LOGIC ("Next hop from " << 
                        v->GetVertexId () << " to network " << w->GetVertexId () <<
                        " via outgoing interface " << outIf <<
                        " with distance " << distance);
          return 1;
        }
    } // end v is the root
  else if (v->GetVertexType () == SATSPFVertex::VertexNetwork) 
    {
// See if any of v's parents are the root
      if (v->GetParent () == m_spfroot)
        {
// 16.1.1 para 5. ...the parent vertex is a network that
// directly connects the calculating router to the destination
// router.  The list of next hops is then determined by
// examining the destination's router-LSA...
          NS_ASSERT (w->GetVertexType () == SATSPFVertex::VertexRouter);
          GlobalRoutingLinkRecord *linkRemote = 0;
          while ((linkRemote = SPFGetNextLink (w, v, linkRemote)))
            {
/* ...For each link in the router-LSA that points back to the
 * parent network, the link's Link Data field provides the IP
 * address of a next hop router.  The outgoing interface to
 * use can then be derived from the next hop IP address (or 
 * it can be inherited from the parent network).
 */
              Ipv4Address nextHop = linkRemote->GetLinkData ();
              uint32_t outIf = v->GetRootExitDirection ().second;
              w->SetRootExitDirection (nextHop, outIf);
              NS_LOG_LOGIC ("Next hop from " <<
                            v->GetVertexId () << " to " << w->GetVertexId () <<
                            " goes through next hop " << nextHop <<
                            " via outgoing interface " << outIf);
            }
        }
      else 
        {
          w->SetRootExitDirection (v->GetRootExitDirection ());
        }
    }
  else 
    {
//
// If we're calculating the next hop information from a node (v) that is 
// *not* the root, then we need to "inherit" the information needed to
// forward the packet from the vertex closer to the root.  That is, we'll
// still send packets to the next hop address of the router adjacent to the
// root on the path toward <w>.
//
// Above, when we were considering the root node, we calculated the next hop
// address and outgoing interface required to get off of the root network.
// At this point, we are further away from the root network along one of the
// (shortest) paths.  So the next hop and outoing interface remain the same
// (are inherited).
//
      w->InheritAllRootExitDirections (v);
    }
//
// In all cases, we need valid values for the distance metric and a parent.
//
  w->SetDistanceFromRoot (distance);
  w->SetParent (v);

  return 1;
}

//
// This method is derived from quagga ospf_get_next_link ()
//
// First search the Global Router Link Records of vertex <v> for one
// representing a point-to point link to vertex <w>.
//
// What is done depends on prev_link.  Contrary to appearances, prev_link just
// acts as a flag here.  If prev_link is NULL, we return the first Global
// Router Link Record we find that describes a point-to-point link from <v> 
// to <w>.  If prev_link is not NULL, we return a Global Router Link Record
// representing a possible *second* link from <v> to <w>.
//
GlobalRoutingLinkRecord*
SatGlobalRouteManagerImpl::SPFGetNextLink (
  SATSPFVertex* v,
  SATSPFVertex* w,
  GlobalRoutingLinkRecord* prev_link) 
{
  NS_LOG_FUNCTION (this << v << w << prev_link);

  bool skip = true;
  bool found_prev_link = false;
  GlobalRoutingLinkRecord* l;
//
// If prev_link is 0, we are really looking for the first link, not the next 
// link.
//
  if (prev_link == 0)
    {
      skip = false;
      found_prev_link = true;
    }
//
// Iterate through the Global Router Link Records advertised by the vertex
// <v> looking for records representing the point-to-point links off of this
// vertex.
//
  for (uint32_t i = 0; i < v->GetLSA ()->GetNLinkRecords (); ++i)
    {
      l = v->GetLSA ()->GetLinkRecord (i);
//
// The link ID of a link record representing a point-to-point link is set to
// the router ID of the neighboring router -- the router to which the link
// connects from the perspective of <v> in this case.  The vertex ID is also
// set to the router ID (using the link state advertisement of a router node).
// We're just checking to see if the link <l> is actually the link from <v> to
// <w>.
//
      if (l->GetLinkId () == w->GetVertexId ()) 
        {
          if (!found_prev_link)
            {
              NS_LOG_LOGIC ("Skipping links before prev_link found");
              found_prev_link = true;
              continue;
            }

          NS_LOG_LOGIC ("Found matching link l:  linkId = " <<
                        l->GetLinkId () << " linkData = " << l->GetLinkData ());
//
// If skip is false, don't (not too surprisingly) skip the link found -- it's 
// the one we're interested in.  That's either because we didn't pass in a 
// previous link, and we're interested in the first one, or because we've 
// skipped a previous link and moved forward to the next (which is then the
// one we want).
//
          if (skip == false)
            {
              NS_LOG_LOGIC ("Returning the found link");
              return l;
            }
          else
            {
//
// Skip is true and we've found a link from <v> to <w>.  We want the next one.
// Setting skip to false gets us the next point-to-point global router link
// record in the LSA from <v>.
//
              NS_LOG_LOGIC ("Skipping the found link");
              skip = false;
              continue;
            }
        }
    }
  return 0;
}

//
// Used for unit tests.
//
void
SatGlobalRouteManagerImpl::DebugSPFCalculate (Ipv4Address root)
{
  NS_LOG_FUNCTION (this << root);
  SPFCalculate (root);
}

//
// Used to test if a node is a stub, from an OSPF sense.
// If there is only one link of type 1 or 2, then a default route
// can safely be added to the next-hop router and SPF does not need
// to be run
//
bool
SatGlobalRouteManagerImpl::CheckForStubNode (Ipv4Address root)
{
  NS_LOG_FUNCTION (this << root);
  GlobalRoutingLSA *rlsa = m_lsdb->GetLSA (root);
  Ipv4Address myRouterId = rlsa->GetLinkStateId ();
  int transits = 0;
  GlobalRoutingLinkRecord *transitLink = 0;
  for (uint32_t i = 0; i < rlsa->GetNLinkRecords (); i++)
    {
      GlobalRoutingLinkRecord *l = rlsa->GetLinkRecord (i);
      if (l->GetLinkType () == GlobalRoutingLinkRecord::TransitNetwork)
        {
          transits++;
          transitLink = l;
        }
      else if (l->GetLinkType () == GlobalRoutingLinkRecord::PointToPoint)
        {
          transits++;
          transitLink = l;
        }
    }
  if (transits == 0)
    {
      // This router is not connected to any router.  Probably, global
      // routing should not be called for this node, but we can just raise
      // a warning here and return true.
      NS_LOG_WARN ("all nodes should have at least one transit link:" << root );
      return true;
    }
  if (transits == 1)
    {
      if (transitLink->GetLinkType () == GlobalRoutingLinkRecord::TransitNetwork)
        {
          // Install default route to next hop router
          // What is the next hop?  We need to check all neighbors on the link.
          // If there is a single router that has two transit links, then
          // that is the default next hop.  If there are more than one
          // routers on link with multiple transit links, return false.
          // Not yet implemented, so simply return false
          NS_LOG_LOGIC ("TBD: Would have inserted default for transit");
          return false;
        }
      else if (transitLink->GetLinkType () == GlobalRoutingLinkRecord::PointToPoint)
        {
          // Install default route to next hop
          // The link record LinkID is the router ID of the peer.
          // The Link Data is the local IP interface address
          GlobalRoutingLSA *w_lsa = m_lsdb->GetLSA (transitLink->GetLinkId ());
          uint32_t nLinkRecords = w_lsa->GetNLinkRecords ();
          for (uint32_t j = 0; j < nLinkRecords; ++j)
            {
              //
              // We are only concerned about point-to-point links
              //
              GlobalRoutingLinkRecord *lr = w_lsa->GetLinkRecord (j);
              if (lr->GetLinkType () != GlobalRoutingLinkRecord::PointToPoint)
                {
                  continue;
                }
              // Find the link record that corresponds to our routerId
              if (lr->GetLinkId () == myRouterId)
                {
                  // Next hop is stored in the LinkID field of lr
                  Ptr<GlobalRouter> router = rlsa->GetNode ()->GetObject<GlobalRouter> ();
                  NS_ASSERT (router);
                  Ptr<Ipv4GlobalRouting> gr = router->GetRoutingProtocol ();
                  NS_ASSERT (gr);
                  gr->AddNetworkRouteTo (Ipv4Address ("0.0.0.0"), Ipv4Mask ("0.0.0.0"), lr->GetLinkData (), 
                                         FindOutgoingInterfaceId (transitLink->GetLinkData ()));
                  NS_LOG_LOGIC ("Inserting default route for node " << myRouterId << " to next hop " << 
                                lr->GetLinkData () << " via interface " << 
                                FindOutgoingInterfaceId (transitLink->GetLinkData ()));
                  return true;
                }
            }
        }
    }
  return false;
}

// quagga ospf_spf_calculate
void
SatGlobalRouteManagerImpl::SPFCalculate (Ipv4Address root)
{
  NS_LOG_FUNCTION (this << root);

  SATSPFVertex *v;
//
// Initialize the Link State Database.
//
  m_lsdb->Initialize ();

//
// The candidate queue is a priority queue of SPFVertex objects, with the top
// of the queue being the closest vertex in terms of distance from the root
// of the tree.  Initially, this queue is empty.
//
  SatCandidateQueue candidate;
  NS_ASSERT (candidate.Size () == 0);

//
// Initialize the shortest-path tree to only contain the router doing the 
// calculation.  Each router (and corresponding network) is a vertex in the
// shortest path first (SPF) tree.
//
  v = new SATSPFVertex (m_lsdb->GetLSA (root));

// 
// This vertex is the root of the SPF tree and it is distance 0 from the root.
// We also mark this vertex as being in the SPF tree.
//
  m_spfroot= v;
  v->SetDistanceFromRoot (0);
  v->GetLSA ()->SetStatus (GlobalRoutingLSA::LSA_SPF_IN_SPFTREE);
  NS_LOG_LOGIC ("Starting SPFCalculate for node " << root);

//
// Optimize SPF calculation, for ns-3.
// We do not need to calculate SPF for every node in the network if this
// node has only one interface through which another router can be 
// reached.  Instead, short-circuit this computation and just install
// a default route in the CheckForStubNode() method.
//
    if (NodeList::GetNNodes () > 0 && CheckForStubNode (root))
    {
      NS_LOG_LOGIC ("SPFCalculate truncated for stub node " << root);
      delete m_spfroot;
      return;
    }

    for (;;)
    {
//
// The operations we need to do are given in the OSPF RFC which we reference
// as we go along.
//
// RFC2328 16.1. (2). 
//
// We examine the Global Router Link Records in the Link State 
// Advertisements of the current vertex.  If there are any point-to-point
// links to unexplored adjacent vertices we add them to the tree and update
// the distance and next hop information on how to get there.  We also add
// the new vertices to the candidate queue (the priority queue ordered by
// shortest path).  If the new vertices represent shorter paths, we use them
// and update the path cost.
//
        SPFNext (v, candidate);

//
// RFC2328 16.1. (3). 
//
// If at this step the candidate list is empty, the shortest-path tree (of
// transit vertices) has been completely built and this stage of the
// procedure terminates. 
//
        if (candidate.Size () == 0)
        {
            break;
        }

//  
// Choose the vertex belonging to the candidate list that is closest to the
// root, and add it to the shortest-path tree (removing it from the candidate
// list in the process).
//
// Recall that in the previous step, we created SPFVertex structures for each
// of the routers found in the Global Router Link Records and added tehm to 
// the candidate list.
//
        NS_LOG_LOGIC (candidate);
        v = candidate.Pop ();
        NS_LOG_LOGIC ("Popped vertex " << v->GetVertexId ());

//
// Update the status field of the vertex to indicate that it is in the SPF
// tree.
//
        v->GetLSA ()->SetStatus (GlobalRoutingLSA::LSA_SPF_IN_SPFTREE);

//
// The current vertex has a parent pointer.  By calling this rather oddly 
// named method (blame quagga) we add the current vertex to the list of 
// children of that parent vertex.  In the next hop calculation called during
// SPFNext, the parent pointer was set but the vertex has been orphaned up
// to now.
//
        SPFVertexAddParent (v);

//
// Note that when there is a choice of vertices closest to the root, network
// vertices must be chosen before router vertices in order to necessarily
// find all equal-cost paths. 
//
// RFC2328 16.1. (4). 
//
// This is the method that actually adds the routes.  It'll walk the list
// of nodes in the system, looking for the node corresponding to the router
// ID of the root of the tree -- that is the router we're building the routes
// for.  It looks for the Ipv4 interface of that node and remembers it.  So
// we are only actually adding routes to that one node at the root of the SPF 
// tree.
//
// We're going to pop of a pointer to every vertex in the tree except the 
// root in order of distance from the root.  For each of the vertices, we call
// SPFIntraAddRouter ().  Down in SPFIntraAddRouter, we look at all of the 
// point-to-point Global Router Link Records (the links to nodes adjacent to
// the node represented by the vertex).  We add a route to the IP address 
// specified by the m_linkData field of each of those link records.  This will
// be the *local* IP address associated with the interface attached to the 
// link.  We use the outbound interface and next hop information present in 
// the vertex <v> which have possibly been inherited from the root.
//
// To summarize, we're going to look at the node represented by <v> and loop
// through its point-to-point links, adding a *host* route to the local IP
// address (at the <v> side) for each of those links.
//
        if (v->GetVertexType () == SATSPFVertex::VertexRouter)
        {
          SPFIntraAddRouter (v);
        }
        else if (v->GetVertexType () == SATSPFVertex::VertexNetwork)
        {
          SPFIntraAddTransit (v);
        }
        else
        {
          NS_ASSERT_MSG (0, "illegal SPFVertex type");
        }

//
// RFC2328 16.1. (5). 
//
// Iterate the algorithm by returning to Step 2 until there are no more
// candidate vertices.

    }  // end for loop

// Second stage of SPF calculation procedure
    SPFProcessStubs (m_spfroot);
    for (uint32_t i = 0; i < m_lsdb->GetNumExtLSAs (); i++)
    {
      m_spfroot->ClearVertexProcessed ();
      GlobalRoutingLSA *extlsa = m_lsdb->GetExtLSA (i);
      NS_LOG_LOGIC ("Processing External LSA with id " << extlsa->GetLinkStateId ());
      ProcessASExternals (m_spfroot, extlsa);
    }


//
// We're all done setting the routing information for the node at the root of
// the SPF tree.  Delete all of the vertices and corresponding resources.  Go
// possibly do it again for the next router.
//
    delete m_spfroot;
    m_spfroot = 0;

}


void
SatGlobalRouteManagerImpl::SPFCalculateforCluster (Ipv4Address root, std::vector<std::vector<std::vector<std::pair<uint32_t, uint32_t>>>> BoundaryNode)
{
    NS_LOG_FUNCTION (this << root);
    // std::cout << "SPFCaculateforCluster test4" << std::endl;
    SATSPFVertex *v;

    // Initialize the Link State Database.
    m_lsdb->Initialize ();


    // The candidate queue is a priority queue of SATSPFVertex objects. Initially, this queue is empty.
    SatCandidateQueue candidate;
    NS_ASSERT (candidate.Size () == 0);


    // Initialize the shortest-path tree to only contain the router doing the 
    // calculation.  Each router is a vertex in the shortest path first (SPF) tree.
    v = new SATSPFVertex (m_lsdb->GetLSA (root));

    // This vertex is the root of the SPF tree and it is distance 0 from the root.
    m_spfroot= v;
    v->SetDistanceFromRoot (0);
    v->GetLSA ()->SetStatus (GlobalRoutingLSA::LSA_SPF_IN_SPFTREE);
    NS_LOG_LOGIC ("Starting SPFCalculate for node " << root);

    // std::cout << "SPFCaculateforCluster test2" << std::endl;
    // Optimize SPF calculation, for ns-3.
    // We do not need to calculate SPF for every node in the network if this
    // node has only one interface through which another router can be 
    // reached.  Instead, short-circuit this computation and just install
    // a default route in the CheckForStubNode() method.

    // !< 0720: 注释了这一行，可能存在分簇结果导致StubNode.
    // if (NodeList::GetNNodes () > 0 && CheckForStubNode (root))
    // {
    //     NS_LOG_LOGIC ("SPFCalculate truncated for stub node " << root);
    //     delete m_spfroot;
    //     return;
    // }
    // std::cout << "SPFCaculateforCluster test1" << std::endl;
    for ( ; ; )
    {
    // Reference: RFC2328 16.1. (2).
        // We examine the Global Router Link Records in the Link State Advertisements of the current vertex.  
        // If there are any point-to-point links to unexplored adjacent vertices we add them to the tree 
        // and update the distance and next hop information on how to get there.  
        // We also add the new vertices to the candidate queue (the priority queue ordered by shortest path).  
        // If the new vertices represent shorter paths, we use them and update the path cost.
        SPFNextforClusters (v, candidate, BoundaryNode);


    // RFC2328 16.1. (3). 
        // If at this step the candidate list is empty, the shortest-path tree has been completely built 
        // This stage of the procedure terminates. 
        if (candidate.Size () == 0)
        {
            break;
        }
 
        // Choose the vertex belonging to the candidate list that is closest to the root, and add it to the shortest-path tree
        // Removing it from the candidate list in the process.
        NS_LOG_LOGIC (candidate);
        v = candidate.Pop ();
        NS_LOG_LOGIC ("Popped vertex " << v->GetVertexId ());

        // Update the status field of the vertex to indicate that it is in the SPF tree.
        v->GetLSA ()->SetStatus (GlobalRoutingLSA::LSA_SPF_IN_SPFTREE);

        // The current vertex has a parent pointer.  
        // In the next hop calculation called during SPFNext, 
        // the parent pointer was set but the vertex has been orphaned up to now.
        SPFVertexAddParent (v);


        // Note that when there is a choice of vertices closest to the root, network
        // vertices must be chosen before router vertices in order to necessarily
        // find all equal-cost paths. 

    // RFC2328 16.1. (4). 
        // This is the method that actually adds the routes.  It'll walk the list
        // of nodes in the system, looking for the node corresponding to the router
        // ID of the root of the tree -- that is the router we're building the routes
        // for.  It looks for the Ipv4 interface of that node and remembers it.  So
        // we are only actually adding routes to that one node at the root of the SPF 
        // tree.
        //
        // We're going to pop of a pointer to every vertex in the tree except the 
        // root in order of distance from the root.  For each of the vertices, we call
        // SPFIntraAddRouter ().  Down in SPFIntraAddRouter, we look at all of the 
        // point-to-point Global Router Link Records (the links to nodes adjacent to
        // the node represented by the vertex).  We add a route to the IP address 
        // specified by the m_linkData field of each of those link records.  

        if (v->GetVertexType () == SATSPFVertex::VertexRouter)
        {
            SPFIntraAddRouter (v);
        }
        else if (v->GetVertexType () == SATSPFVertex::VertexNetwork)
        {
            SPFIntraAddTransit (v);
        }
        else
        {
            NS_ASSERT_MSG (0, "illegal SPFVertex type");
        }

 
    // RFC2328 16.1. (5). 
        // Iterate the algorithm by returning to Step 2 until there are no more
        // candidate vertices.

    }  // end for loop

    // Second stage of SPF calculation procedure
    SPFProcessStubs (m_spfroot);
    for (uint32_t i = 0; i < m_lsdb->GetNumExtLSAs (); i++)
    {
        m_spfroot->ClearVertexProcessed ();
        GlobalRoutingLSA *extlsa = m_lsdb->GetExtLSA (i);
        NS_LOG_LOGIC ("Processing External LSA with id " << extlsa->GetLinkStateId ());
        ProcessASExternals (m_spfroot, extlsa);
    }

    // We're all done setting the routing information for the node at the root of the SPF tree.  
    // Delete all of the vertices and corresponding resources.  
    // Go possibly do it again for the next router.
    delete m_spfroot;
    m_spfroot = 0;
}



void
SatGlobalRouteManagerImpl::ProcessASExternals (SATSPFVertex* v, GlobalRoutingLSA* extlsa)
{
  NS_LOG_FUNCTION (this << v << extlsa);
  NS_LOG_LOGIC ("Processing external for destination " << 
                extlsa->GetLinkStateId () <<
                ", for router "  << v->GetVertexId () <<
                ", advertised by " << extlsa->GetAdvertisingRouter ());
  if (v->GetVertexType () == SATSPFVertex::VertexRouter)
    {
      GlobalRoutingLSA *rlsa = v->GetLSA ();
      NS_LOG_LOGIC ("Processing router LSA with id " << rlsa->GetLinkStateId ());
      if ((rlsa->GetLinkStateId ()) == (extlsa->GetAdvertisingRouter ()))
        {
          NS_LOG_LOGIC ("Found advertising router to destination");
          SPFAddASExternal (extlsa,v);
        }
    }
  for (uint32_t i = 0; i < v->GetNChildren (); i++)
    {
      if (!v->GetChild (i)->IsVertexProcessed ())
        {
          NS_LOG_LOGIC ("Vertex's child " << i << " not yet processed, processing...");
          ProcessASExternals (v->GetChild (i), extlsa);
          v->GetChild (i)->SetVertexProcessed (true);
        }
    }
}

//
// Adding external routes to routing table - modeled after
// SPFAddIntraAddStub()
//

void
SatGlobalRouteManagerImpl::SPFAddASExternal (GlobalRoutingLSA *extlsa, SATSPFVertex *v)
{
  NS_LOG_FUNCTION (this << extlsa << v);

  NS_ASSERT_MSG (m_spfroot, "SatGlobalRouteManagerImpl::SPFAddASExternal (): Root pointer not set");
// Two cases to consider: We are advertising the external ourselves
// => No need to add anything
// OR find best path to the advertising router
  if (v->GetVertexId () == m_spfroot->GetVertexId ())
    {
      NS_LOG_LOGIC ("External is on local host: " 
                    << v->GetVertexId () << "; returning");
      return;
    }
  NS_LOG_LOGIC ("External is on remote host: " 
                << extlsa->GetAdvertisingRouter () << "; installing");

  Ipv4Address routerId = m_spfroot->GetVertexId ();

  NS_LOG_LOGIC ("Vertex ID = " << routerId);
//
// We need to walk the list of nodes looking for the one that has the router
// ID corresponding to the root vertex.  This is the one we're going to write
// the routing information to.
//
  NodeList::Iterator i = NodeList::Begin (); 
  NodeList::Iterator listEnd = NodeList::End ();
  for (; i != listEnd; i++)
    {
      Ptr<Node> node = *i;
//
// The router ID is accessible through the GlobalRouter interface, so we need
// to QI for that interface.  If there's no GlobalRouter interface, the node
// in question cannot be the router we want, so we continue.
// 
      Ptr<GlobalRouter> rtr = node->GetObject<GlobalRouter> ();

      if (rtr == 0)
        {
          NS_LOG_LOGIC ("No GlobalRouter interface on node " << node->GetId ());
          continue;
        }
//
// If the router ID of the current node is equal to the router ID of the 
// root of the SPF tree, then this node is the one for which we need to 
// write the routing tables.
//
      NS_LOG_LOGIC ("Considering router " << rtr->GetRouterId ());

      if (rtr->GetRouterId () == routerId)
        {
          NS_LOG_LOGIC ("Setting routes for node " << node->GetId ());
//
// Routing information is updated using the Ipv4 interface.  We need to QI
// for that interface.  If the node is acting as an IP version 4 router, it
// should absolutely have an Ipv4 interface.
//
          Ptr<Ipv4> ipv4 = node->GetObject<Ipv4> ();
          NS_ASSERT_MSG (ipv4, 
                         "SatGlobalRouteManagerImpl::SPFIntraAddRouter (): "
                         "QI for <Ipv4> interface failed");
//
// Get the Global Router Link State Advertisement from the vertex we're
// adding the routes to.  The LSA will have a number of attached Global Router
// Link Records corresponding to links off of that vertex / node.  We're going
// to be interested in the records corresponding to point-to-point links.
//
          NS_ASSERT_MSG (v->GetLSA (), 
                         "SatGlobalRouteManagerImpl::SPFIntraAddRouter (): "
                         "Expected valid LSA in SATSPFVertex* v");
          Ipv4Mask tempmask = extlsa->GetNetworkLSANetworkMask ();
          Ipv4Address tempip = extlsa->GetLinkStateId ();
          tempip = tempip.CombineMask (tempmask);

//
// Here's why we did all of that work.  We're going to add a host route to the
// host address found in the m_linkData field of the point-to-point link
// record.  In the case of a point-to-point link, this is the local IP address
// of the node connected to the link.  Each of these point-to-point links
// will correspond to a local interface that has an IP address to which
// the node at the root of the SPF tree can send packets.  The vertex <v> 
// (corresponding to the node that has these links and interfaces) has 
// an m_nextHop address precalculated for us that is the address to which the
// root node should send packets to be forwarded to these IP addresses.
// Similarly, the vertex <v> has an m_rootOif (outbound interface index) to
// which the packets should be send for forwarding.
//
          Ptr<GlobalRouter> router = node->GetObject<GlobalRouter> ();
          if (router == 0)
            {
              continue;
            }
          Ptr<Ipv4GlobalRouting> gr = router->GetRoutingProtocol ();
          NS_ASSERT (gr);
          // walk through all next-hop-IPs and out-going-interfaces for reaching
          // the stub network gateway 'v' from the root node
          for (uint32_t i = 0; i < v->GetNRootExitDirections (); i++)
            {
              SATSPFVertex::NodeExit_t exit = v->GetRootExitDirection (i);
              Ipv4Address nextHop = exit.first;
              int32_t outIf = exit.second;
              if (outIf >= 0)
                {
                  gr->AddASExternalRouteTo (tempip, tempmask, nextHop, outIf);
                  NS_LOG_LOGIC ("(Route " << i << ") Node " << node->GetId () <<
                                " add external network route to " << tempip <<
                                " using next hop " << nextHop <<
                                " via interface " << outIf);
                }
              else
                {
                  NS_LOG_LOGIC ("(Route " << i << ") Node " << node->GetId () <<
                                " NOT able to add network route to " << tempip <<
                                " using next hop " << nextHop <<
                                " since outgoing interface id is negative");
                }
            }
          return;
        } // if
    } // for
}


// Processing logic from RFC 2328, page 166 and quagga ospf_spf_process_stubs ()
// stub link records will exist for point-to-point interfaces and for
// broadcast interfaces for which no neighboring router can be found
void
SatGlobalRouteManagerImpl::SPFProcessStubs (SATSPFVertex* v)
{
  NS_LOG_FUNCTION (this << v);
  NS_LOG_LOGIC ("Processing stubs for " << v->GetVertexId ());
  if (v->GetVertexType () == SATSPFVertex::VertexRouter)
    {
      GlobalRoutingLSA *rlsa = v->GetLSA ();
      NS_LOG_LOGIC ("Processing router LSA with id " << rlsa->GetLinkStateId ());
      for (uint32_t i = 0; i < rlsa->GetNLinkRecords (); i++)
        {
          NS_LOG_LOGIC ("Examining link " << i << " of " << 
                        v->GetVertexId () << "'s " <<
                        v->GetLSA ()->GetNLinkRecords () << " link records");
          GlobalRoutingLinkRecord *l = v->GetLSA ()->GetLinkRecord (i);
          if (l->GetLinkType () == GlobalRoutingLinkRecord::StubNetwork)
            {
              NS_LOG_LOGIC ("Found a Stub record to " << l->GetLinkId ());
              SPFIntraAddStub (l, v);
              continue;
            }
        }
    }
  for (uint32_t i = 0; i < v->GetNChildren (); i++)
    {
      if (!v->GetChild (i)->IsVertexProcessed ())
        {
          SPFProcessStubs (v->GetChild (i));
          v->GetChild (i)->SetVertexProcessed (true);
        }
    }
}

// RFC2328 16.1. second stage. 
void
SatGlobalRouteManagerImpl::SPFIntraAddStub (GlobalRoutingLinkRecord *l, SATSPFVertex* v)
{
  NS_LOG_FUNCTION (this << l << v);

  NS_ASSERT_MSG (m_spfroot, 
                 "SatGlobalRouteManagerImpl::SPFIntraAddStub (): Root pointer not set");

  // XXX simplifed logic for the moment.  There are two cases to consider:
  // 1) the stub network is on this router; do nothing for now
  //    (already handled above)
  // 2) the stub network is on a remote router, so I should use the
  // same next hop that I use to get to vertex v
  if (v->GetVertexId () == m_spfroot->GetVertexId ())
    {
      NS_LOG_LOGIC ("Stub is on local host: " << v->GetVertexId () << "; returning");
      return;
    }
  NS_LOG_LOGIC ("Stub is on remote host: " << v->GetVertexId () << "; installing");
//
// The root of the Shortest Path First tree is the router to which we are 
// going to write the actual routing table entries.  The vertex corresponding
// to this router has a vertex ID which is the router ID of that node.  We're
// going to use this ID to discover which node it is that we're actually going
// to update.
//
  Ipv4Address routerId = m_spfroot->GetVertexId ();

  NS_LOG_LOGIC ("Vertex ID = " << routerId);
//
// We need to walk the list of nodes looking for the one that has the router
// ID corresponding to the root vertex.  This is the one we're going to write
// the routing information to.
//
  NodeList::Iterator i = NodeList::Begin (); 
  NodeList::Iterator listEnd = NodeList::End ();
  for (; i != listEnd; i++)
    {
      Ptr<Node> node = *i;
//
// The router ID is accessible through the GlobalRouter interface, so we need
// to QI for that interface.  If there's no GlobalRouter interface, the node
// in question cannot be the router we want, so we continue.
// 
      Ptr<GlobalRouter> rtr = 
        node->GetObject<GlobalRouter> ();

      if (rtr == 0)
        {
          NS_LOG_LOGIC ("No GlobalRouter interface on node " << 
                        node->GetId ());
          continue;
        }
//
// If the router ID of the current node is equal to the router ID of the 
// root of the SPF tree, then this node is the one for which we need to 
// write the routing tables.
//
      NS_LOG_LOGIC ("Considering router " << rtr->GetRouterId ());

      if (rtr->GetRouterId () == routerId)
        {
          NS_LOG_LOGIC ("Setting routes for node " << node->GetId ());
//
// Routing information is updated using the Ipv4 interface.  We need to QI
// for that interface.  If the node is acting as an IP version 4 router, it
// should absolutely have an Ipv4 interface.
//
          Ptr<Ipv4> ipv4 = node->GetObject<Ipv4> ();
          NS_ASSERT_MSG (ipv4, 
                         "SatGlobalRouteManagerImpl::SPFIntraAddRouter (): "
                         "QI for <Ipv4> interface failed");
//
// Get the Global Router Link State Advertisement from the vertex we're
// adding the routes to.  The LSA will have a number of attached Global Router
// Link Records corresponding to links off of that vertex / node.  We're going
// to be interested in the records corresponding to point-to-point links.
//
          NS_ASSERT_MSG (v->GetLSA (), 
                         "SatGlobalRouteManagerImpl::SPFIntraAddRouter (): "
                         "Expected valid LSA in SATSPFVertex* v");
          Ipv4Mask tempmask (l->GetLinkData ().Get ());
          Ipv4Address tempip = l->GetLinkId ();
          tempip = tempip.CombineMask (tempmask);
//
// Here's why we did all of that work.  We're going to add a host route to the
// host address found in the m_linkData field of the point-to-point link
// record.  In the case of a point-to-point link, this is the local IP address
// of the node connected to the link.  Each of these point-to-point links
// will correspond to a local interface that has an IP address to which
// the node at the root of the SPF tree can send packets.  The vertex <v> 
// (corresponding to the node that has these links and interfaces) has 
// an m_nextHop address precalculated for us that is the address to which the
// root node should send packets to be forwarded to these IP addresses.
// Similarly, the vertex <v> has an m_rootOif (outbound interface index) to
// which the packets should be send for forwarding.
//

          Ptr<GlobalRouter> router = node->GetObject<GlobalRouter> ();
          if (router == 0)
            {
              continue;
            }
          Ptr<Ipv4GlobalRouting> gr = router->GetRoutingProtocol ();
          NS_ASSERT (gr);
          // walk through all next-hop-IPs and out-going-interfaces for reaching
          // the stub network gateway 'v' from the root node
          for (uint32_t i = 0; i < v->GetNRootExitDirections (); i++)
            {
              SATSPFVertex::NodeExit_t exit = v->GetRootExitDirection (i);
              Ipv4Address nextHop = exit.first;
              int32_t outIf = exit.second;
              if (outIf >= 0)
                {
                  gr->AddNetworkRouteTo (tempip, tempmask, nextHop, outIf);
                  NS_LOG_LOGIC ("(Route " << i << ") Node " << node->GetId () <<
                                " add network route to " << tempip <<
                                " using next hop " << nextHop <<
                                " via interface " << outIf);
                }
              else
                {
                  NS_LOG_LOGIC ("(Route " << i << ") Node " << node->GetId () <<
                                " NOT able to add network route to " << tempip <<
                                " using next hop " << nextHop <<
                                " since outgoing interface id is negative");
                }
            }
          return;
        } // if
    } // for
}

//
// Return the interface number corresponding to a given IP address and mask
// This is a wrapper around GetInterfaceForPrefix(), but we first
// have to find the right node pointer to pass to that function.
// If no such interface is found, return -1 (note:  unit test framework
// for routing assumes -1 to be a legal return value)
//
int32_t
SatGlobalRouteManagerImpl::FindOutgoingInterfaceId (Ipv4Address a, Ipv4Mask amask)
{
  NS_LOG_FUNCTION (this << a << amask);
//
// We have an IP address <a> and a vertex ID of the root of the SPF tree.
// The question is what interface index does this address correspond to.
// The answer is a little complicated since we have to find a pointer to
// the node corresponding to the vertex ID, find the Ipv4 interface on that
// node in order to iterate the interfaces and find the one corresponding to
// the address in question.
//
  Ipv4Address routerId = m_spfroot->GetVertexId ();
//
// Walk the list of nodes in the system looking for the one corresponding to
// the node at the root of the SPF tree.  This is the node for which we are
// building the routing table.
//
  NodeList::Iterator i = NodeList::Begin (); 
  NodeList::Iterator listEnd = NodeList::End ();
  for (; i != listEnd; i++)
    {
      Ptr<Node> node = *i;

      Ptr<GlobalRouter> rtr = 
        node->GetObject<GlobalRouter> ();
//
// If the node doesn't have a GlobalRouter interface it can't be the one
// we're interested in.
//
      if (rtr == 0)
        {
          continue;
        }

      if (rtr->GetRouterId () == routerId)
        {
//
// This is the node we're building the routing table for.  We're going to need
// the Ipv4 interface to look for the ipv4 interface index.  Since this node
// is participating in routing IP version 4 packets, it certainly must have 
// an Ipv4 interface.
//
          Ptr<Ipv4> ipv4 = node->GetObject<Ipv4> ();
          NS_ASSERT_MSG (ipv4, 
                         "SatGlobalRouteManagerImpl::FindOutgoingInterfaceId (): "
                         "GetObject for <Ipv4> interface failed");
//
// Look through the interfaces on this node for one that has the IP address
// we're looking for.  If we find one, return the corresponding interface
// index, or -1 if not found.
//
          int32_t interface = ipv4->GetInterfaceForPrefix (a, amask);

#if 0
          if (interface < 0)
            {
              NS_FATAL_ERROR ("SatGlobalRouteManagerImpl::FindOutgoingInterfaceId(): "
                              "Expected an interface associated with address a:" << a);
            }
#endif 
          return interface;
        }
    }
//
// Couldn't find it.
//
  NS_LOG_LOGIC ("FindOutgoingInterfaceId():Can't find root node " << routerId);
  return -1;
}

//
// This method is derived from quagga ospf_intra_add_router ()
//
// This is where we are actually going to add the host routes to the routing
// tables of the individual nodes.
//
// The vertex passed as a parameter has just been added to the SPF tree.
// This vertex must have a valid m_root_oid, corresponding to the outgoing
// interface on the root router of the tree that is the first hop on the path
// to the vertex.  The vertex must also have a next hop address, corresponding
// to the next hop on the path to the vertex.  The vertex has an m_lsa field
// that has some number of link records.  For each point to point link record,
// the m_linkData is the local IP address of the link.  This corresponds to
// a destination IP address, reachable from the root, to which we add a host
// route.
//
void
SatGlobalRouteManagerImpl::SPFIntraAddRouter (SATSPFVertex* v)
{
  NS_LOG_FUNCTION (this << v);

  NS_ASSERT_MSG (m_spfroot, 
                 "SatGlobalRouteManagerImpl::SPFIntraAddRouter (): Root pointer not set");
//
// The root of the Shortest Path First tree is the router to which we are 
// going to write the actual routing table entries.  The vertex corresponding
// to this router has a vertex ID which is the router ID of that node.  We're
// going to use this ID to discover which node it is that we're actually going
// to update.
//
  Ipv4Address routerId = m_spfroot->GetVertexId ();

  NS_LOG_LOGIC ("Vertex ID = " << routerId);
//
// We need to walk the list of nodes looking for the one that has the router
// ID corresponding to the root vertex.  This is the one we're going to write
// the routing information to.
//
  NodeList::Iterator i = NodeList::Begin (); 
  NodeList::Iterator listEnd = NodeList::End ();
  for (; i != listEnd; i++)
    {
      Ptr<Node> node = *i;
//
// The router ID is accessible through the GlobalRouter interface, so we need
// to GetObject for that interface.  If there's no GlobalRouter interface, 
// the node in question cannot be the router we want, so we continue.
// 
      Ptr<GlobalRouter> rtr = 
        node->GetObject<GlobalRouter> ();

      if (rtr == 0)
        {
          NS_LOG_LOGIC ("No GlobalRouter interface on node " << 
                        node->GetId ());
          continue;
        }
//
// If the router ID of the current node is equal to the router ID of the 
// root of the SPF tree, then this node is the one for which we need to 
// write the routing tables.
//
      NS_LOG_LOGIC ("Considering router " << rtr->GetRouterId ());

      if (rtr->GetRouterId () == routerId)
        {
          NS_LOG_LOGIC ("Setting routes for node " << node->GetId ());
//
// Routing information is updated using the Ipv4 interface.  We need to 
// GetObject for that interface.  If the node is acting as an IP version 4 
// router, it should absolutely have an Ipv4 interface.
//
          Ptr<Ipv4> ipv4 = node->GetObject<Ipv4> ();
          NS_ASSERT_MSG (ipv4, 
                         "SatGlobalRouteManagerImpl::SPFIntraAddRouter (): "
                         "GetObject for <Ipv4> interface failed");
//
// Get the Global Router Link State Advertisement from the vertex we're
// adding the routes to.  The LSA will have a number of attached Global Router
// Link Records corresponding to links off of that vertex / node.  We're going
// to be interested in the records corresponding to point-to-point links.
//
          GlobalRoutingLSA *lsa = v->GetLSA ();
          NS_ASSERT_MSG (lsa, 
                         "SatGlobalRouteManagerImpl::SPFIntraAddRouter (): "
                         "Expected valid LSA in SATSPFVertex* v");

          uint32_t nLinkRecords = lsa->GetNLinkRecords ();
//
// Iterate through the link records on the vertex to which we're going to add
// routes.  To make sure we're being clear, we're going to add routing table
// entries to the tables on the node corresping to the root of the SPF tree.
// These entries will have routes to the IP addresses we find from looking at
// the local side of the point-to-point links found on the node described by
// the vertex <v>.
//
          NS_LOG_LOGIC (" Node " << node->GetId () <<
                        " found " << nLinkRecords << " link records in LSA " << lsa << "with LinkStateId "<< lsa->GetLinkStateId ());
          for (uint32_t j = 0; j < nLinkRecords; ++j)
            {
//
// We are only concerned about point-to-point links
//
              GlobalRoutingLinkRecord *lr = lsa->GetLinkRecord (j);
              if (lr->GetLinkType () != GlobalRoutingLinkRecord::PointToPoint)
                {
                  continue;
                }
//
// Here's why we did all of that work.  We're going to add a host route to the
// host address found in the m_linkData field of the point-to-point link
// record.  In the case of a point-to-point link, this is the local IP address
// of the node connected to the link.  Each of these point-to-point links
// will correspond to a local interface that has an IP address to which
// the node at the root of the SPF tree can send packets.  The vertex <v> 
// (corresponding to the node that has these links and interfaces) has 
// an m_nextHop address precalculated for us that is the address to which the
// root node should send packets to be forwarded to these IP addresses.
// Similarly, the vertex <v> has an m_rootOif (outbound interface index) to
// which the packets should be send for forwarding.
//
              Ptr<GlobalRouter> router = node->GetObject<GlobalRouter> ();
              if (router == 0)
                {
                  continue;
                }
              Ptr<Ipv4GlobalRouting> gr = router->GetRoutingProtocol ();
              NS_ASSERT (gr);
              // walk through all available exit directions due to ECMP,
              // and add host route for each of the exit direction toward
              // the vertex 'v'
              for (uint32_t i = 0; i < v->GetNRootExitDirections (); i++)
                {
                    SATSPFVertex::NodeExit_t exit = v->GetRootExitDirection (i);
                    Ipv4Address nextHop = exit.first;
                    int32_t outIf = exit.second;
                    if (outIf >= 0)
                    {
                        gr->AddHostRouteTo (lr->GetLinkData (), nextHop,
                                          outIf);

                        Ipv4Address backUpNextHop;
                        backUpNextHop.GetZero();
                        int32_t backUpOutIf = -1;
                        #ifdef _rouXW_backup_
                            uint32_t NumInterface = node->GetObject<Ipv4>()->GetNInterfaces();
                            uint32_t curSatID = node->GetId() - Indexsat;        // ID从0开始
                            // uint32_t dstSatID = GetIdFromIp(lr->GetLinkData ()) - Indexsat;
                            Ptr<Node> dstnode = GetNodeFromIP(lr->GetLinkData ());
                            // 获取备份路径
                            backUpOutIf = GetBackupInterface(node, dstnode, outIf, Indexsat, 0, NumOfOrbit, SatPerOrbit);
                            if((backUpOutIf > 0) && (curSatID < NumOfOrbit*SatPerOrbit))
                            {
                                // std::cout << "curSatID: " << curSatID << ", backUpOutIf: " << backUpOutIf << std::endl;
                                backUpNextHop = GetNeiIpFromInterface(node, backUpOutIf);
                                for(uint32_t i = 1; i < NumInterface; ++i)
                                {
                                    // 添加备份路由表项                            
                                    gr->AddHostRouteTo(lr->GetLinkData (), backUpNextHop, backUpOutIf);    
                                }
                            }


                            // std::cout << "(Route " << i << ") Node " << node->GetId () <<
                            //             " adding host route to " << lr->GetLinkData () <<
                            //             " using next hop " << nextHop <<
                            //             " and outgoing interface " << outIf <<
                            //             " using backup next hop " << backUpNextHop <<
                            //             " and backup outgoing interface " << backUpOutIf << std::endl;
                        #endif

                        NS_LOG_LOGIC ("(Route " << i << ") Node " << node->GetId () <<
                                    " adding host route to " << lr->GetLinkData () <<
                                    " using next hop " << nextHop <<
                                    " and outgoing interface " << outIf <<
                                    " using backup next hop " << backUpNextHop <<
                                    " and backup outgoing interface " << backUpOutIf);
                    }
                    else
                    {
                      NS_LOG_LOGIC ("(Route " << i << ") Node " << node->GetId () <<
                                    " NOT able to add host route to " << lr->GetLinkData () <<
                                    " using next hop " << nextHop <<
                                    " since outgoing interface id is negative " << outIf);
                    }
                } // for all routes from the root the vertex 'v'
            }
//
// Done adding the routes for the selected node.
//
          return;
        }
    }
}
void
SatGlobalRouteManagerImpl::SPFIntraAddTransit (SATSPFVertex* v)
{
  NS_LOG_FUNCTION (this << v);

  NS_ASSERT_MSG (m_spfroot, 
                 "SatGlobalRouteManagerImpl::SPFIntraAddTransit (): Root pointer not set");
//
// The root of the Shortest Path First tree is the router to which we are 
// going to write the actual routing table entries.  The vertex corresponding
// to this router has a vertex ID which is the router ID of that node.  We're
// going to use this ID to discover which node it is that we're actually going
// to update.
//
  Ipv4Address routerId = m_spfroot->GetVertexId ();

  NS_LOG_LOGIC ("Vertex ID = " << routerId);
//
// We need to walk the list of nodes looking for the one that has the router
// ID corresponding to the root vertex.  This is the one we're going to write
// the routing information to.
//
  NodeList::Iterator i = NodeList::Begin (); 
  NodeList::Iterator listEnd = NodeList::End ();
  for (; i != listEnd; i++)
    {
      Ptr<Node> node = *i;
//
// The router ID is accessible through the GlobalRouter interface, so we need
// to GetObject for that interface.  If there's no GlobalRouter interface, 
// the node in question cannot be the router we want, so we continue.
// 
      Ptr<GlobalRouter> rtr = 
        node->GetObject<GlobalRouter> ();

      if (rtr == 0)
        {
          NS_LOG_LOGIC ("No GlobalRouter interface on node " << 
                        node->GetId ());
          continue;
        }
//
// If the router ID of the current node is equal to the router ID of the 
// root of the SPF tree, then this node is the one for which we need to 
// write the routing tables.
//
      NS_LOG_LOGIC ("Considering router " << rtr->GetRouterId ());

      if (rtr->GetRouterId () == routerId)
        {
          NS_LOG_LOGIC ("setting routes for node " << node->GetId ());
//
// Routing information is updated using the Ipv4 interface.  We need to 
// GetObject for that interface.  If the node is acting as an IP version 4 
// router, it should absolutely have an Ipv4 interface.
//
          Ptr<Ipv4> ipv4 = node->GetObject<Ipv4> ();
          NS_ASSERT_MSG (ipv4, 
                         "SatGlobalRouteManagerImpl::SPFIntraAddTransit (): "
                         "GetObject for <Ipv4> interface failed");
//
// Get the Global Router Link State Advertisement from the vertex we're
// adding the routes to.  The LSA will have a number of attached Global Router
// Link Records corresponding to links off of that vertex / node.  We're going
// to be interested in the records corresponding to point-to-point links.
//
          GlobalRoutingLSA *lsa = v->GetLSA ();
          NS_ASSERT_MSG (lsa, 
                         "SatGlobalRouteManagerImpl::SPFIntraAddTransit (): "
                         "Expected valid LSA in SATSPFVertex* v");
          Ipv4Mask tempmask = lsa->GetNetworkLSANetworkMask ();
          Ipv4Address tempip = lsa->GetLinkStateId ();
          tempip = tempip.CombineMask (tempmask);
          Ptr<GlobalRouter> router = node->GetObject<GlobalRouter> ();
          if (router == 0)
            {
              continue;
            }
          Ptr<Ipv4GlobalRouting> gr = router->GetRoutingProtocol ();
          NS_ASSERT (gr);
          // walk through all available exit directions due to ECMP,
          // and add host route for each of the exit direction toward
          // the vertex 'v'
          for (uint32_t i = 0; i < v->GetNRootExitDirections (); i++)
            {
              SATSPFVertex::NodeExit_t exit = v->GetRootExitDirection (i);
              Ipv4Address nextHop = exit.first;
              int32_t outIf = exit.second;

              if (outIf >= 0)
                {
                  gr->AddNetworkRouteTo (tempip, tempmask, nextHop, outIf);
                  NS_LOG_LOGIC ("(Route " << i << ") Node " << node->GetId () <<
                                " add network route to " << tempip <<
                                " using next hop " << nextHop <<
                                " via interface " << outIf);
                }
              else
                {
                  NS_LOG_LOGIC ("(Route " << i << ") Node " << node->GetId () <<
                                " NOT able to add network route to " << tempip <<
                                " using next hop " << nextHop <<
                                " since outgoing interface id is negative " << outIf);
                }
            }
        }
    } 
}

// Derived from quagga ospf_vertex_add_parents ()
//
// This is a somewhat oddly named method (blame quagga).  Although you might
// expect it to add a parent *to* something, it actually adds a vertex
// to the list of children *in* each of its parents. 
//
// Given a pointer to a vertex, it links back to the vertex's parent that it
// already has set and adds itself to that vertex's list of children.
//
void
SatGlobalRouteManagerImpl::SPFVertexAddParent (SATSPFVertex* v)
{
  NS_LOG_FUNCTION (this << v);

  for (uint32_t i=0;;)
    {
      SATSPFVertex* parent;
      // check if all parents of vertex v
      if ((parent = v->GetParent (i++)) == 0) break;
      parent->AddChild (v);
    }
}

} // namespace ns3