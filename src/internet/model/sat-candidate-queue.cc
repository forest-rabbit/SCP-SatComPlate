/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright 2007 University of Washington
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
 */

#include <algorithm>
#include <iostream>
#include "ns3/log.h"
#include "ns3/assert.h"
#include "sat-candidate-queue.h"
#include "sat-global-route-manager-impl.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("SatCandidateQueue");

/**
 * \brief Stream insertion operator.
 *
 * \param os the reference to the output stream
 * \param t the SATSPFVertex type
 * \returns the reference to the output stream
 */
std::ostream&
operator<< (std::ostream& os, const SATSPFVertex::VertexType& t)
{
  switch (t)
    {
    case SATSPFVertex::VertexRouter:  os << "router"; break;
    case SATSPFVertex::VertexNetwork: os << "network"; break;
    default:                       os << "unknown"; break;
    };
  return os;
}

std::ostream& 
operator<< (std::ostream& os, const SatCandidateQueue& q)
{
  typedef SatCandidateQueue::CandidateList_t List_t;
  typedef List_t::const_iterator CIter_t;
  const SatCandidateQueue::CandidateList_t& list = q.m_candidates;

  os << "*** SatCandidateQueue Begin (<id, distance, LSA-type>) ***" << std::endl;
  for (CIter_t iter = list.begin (); iter != list.end (); iter++)
    {
      os << "<" 
      << (*iter)->GetVertexId () << ", "
      << (*iter)->GetDistanceFromRoot () << ", "
      << (*iter)->GetVertexType () << ">" << std::endl;
    }
  os << "*** SatCandidateQueue End ***";
  return os;
}

SatCandidateQueue::SatCandidateQueue()
  : m_candidates ()
{
  NS_LOG_FUNCTION (this);
}

SatCandidateQueue::~SatCandidateQueue()
{
  NS_LOG_FUNCTION (this);
  Clear ();
}

void
SatCandidateQueue::Clear (void)
{
  NS_LOG_FUNCTION (this);
  while (!m_candidates.empty ())
    {
      SATSPFVertex *p = Pop ();
      delete p;
      p = 0;
    }
}

void
SatCandidateQueue::Push (SATSPFVertex *vNew)
{
  NS_LOG_FUNCTION (this << vNew);

  CandidateList_t::iterator i = std::upper_bound (
      m_candidates.begin (), m_candidates.end (), vNew,
      &SatCandidateQueue::CompareSATSPFVertex
      );
  m_candidates.insert (i, vNew);
}

SATSPFVertex *
SatCandidateQueue::Pop (void)
{
  NS_LOG_FUNCTION (this);
  if (m_candidates.empty ())
    {
      return 0;
    }

  SATSPFVertex *v = m_candidates.front ();
  m_candidates.pop_front ();
  return v;
}

SATSPFVertex *
SatCandidateQueue::Top (void) const
{
  NS_LOG_FUNCTION (this);
  if (m_candidates.empty ())
    {
      return 0;
    }

  return m_candidates.front ();
}

bool
SatCandidateQueue::Empty (void) const
{
  NS_LOG_FUNCTION (this);
  return m_candidates.empty ();
}

uint32_t
SatCandidateQueue::Size (void) const
{
  NS_LOG_FUNCTION (this);
  return m_candidates.size ();
}

SATSPFVertex *
SatCandidateQueue::Find (const Ipv4Address addr) const
{
  NS_LOG_FUNCTION (this);
  CandidateList_t::const_iterator i = m_candidates.begin ();

  for (; i != m_candidates.end (); i++)
    {
      SATSPFVertex *v = *i;
      if (v->GetVertexId () == addr)
        {
          return v;
        }
    }

  return 0;
}

void
SatCandidateQueue::Reorder (void)
{
  NS_LOG_FUNCTION (this);

  m_candidates.sort (&SatCandidateQueue::CompareSATSPFVertex);
  NS_LOG_LOGIC ("After reordering the CandidateQueue");
  NS_LOG_LOGIC (*this);
}

/*
 * In this implementation, SATSPFVertex follows the ordering where
 * a vertex is ranked first if its GetDistanceFromRoot () is smaller;
 * In case of a tie, NetworkLSA is always ranked before RouterLSA.
 *
 * This ordering is necessary for implementing ECMP
 */
bool 
SatCandidateQueue::CompareSATSPFVertex (const SATSPFVertex* v1, const SATSPFVertex* v2)
{
  NS_LOG_FUNCTION (&v1 << &v2);

  bool result = false;
  if (v1->GetDistanceFromRoot () < v2->GetDistanceFromRoot ())
    {
      result = true;
    }
  else if (v1->GetDistanceFromRoot () == v2->GetDistanceFromRoot ())
    {
      if (v1->GetVertexType () == SATSPFVertex::VertexNetwork 
          && v2->GetVertexType () == SATSPFVertex::VertexRouter)
        {
          result = true;
        }
    }
  return result;
}

} // namespace ns3
