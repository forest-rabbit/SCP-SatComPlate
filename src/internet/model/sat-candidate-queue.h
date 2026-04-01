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
 *
 * Author:  Craig Dowell (craigdo@ee.washington.edu)
 */

#ifndef SAT_CANDIDATE_QUEUE_H
#define SAT_CANDIDATE_QUEUE_H

#include <stdint.h>
#include <list>
#include "ns3/ipv4-address.h"

namespace ns3 {

class SATSPFVertex;

/**
 * \ingroup globalrouting
 *
 * \brief A Candidate Queue used in routing calculations.
 *
 * The CandidateQueue is used in the OSPF shortest path computations.  It
 * is a priority queue used to store candidates for the shortest path to a
 * given network.
 *
 * The queue holds Shortest Path First Vertex pointers and orders them
 * according to the lowest value of the field m_distanceFromRoot.  Remaining
 * vertices are ordered according to increasing distance.  This implements a
 * priority queue.
 *
 * Although a STL priority_queue almost does what we want, the requirement
 * for a Find () operation, the dynamic nature of the data and the derived
 * requirement for a Reorder () operation led us to implement this simple 
 * enhanced priority queue.
 */
class SatCandidateQueue
{
public:
/**
 * @brief Create an empty SPF Candidate Queue.
 *
 * @see SATSPFVertex
 */
  SatCandidateQueue ();

/**
 * @brief Destroy an SPF Candidate Queue and release any resources held 
 * by the contents.
 *
 * @see SATSPFVertex
 */
  virtual ~SatCandidateQueue ();

/**
 * @brief Empty the Candidate Queue and release all of the resources 
 * associated with the Shortest Path First Vertex pointers in the queue.
 *
 * @see SATSPFVertex
 */
  void Clear (void);

/**
 * @brief Push a Shortest Path First Vertex pointer onto the queue according
 * to the priority scheme.
 * 
 * On completion, the top of the queue will hold the Shortest Path First
 * Vertex pointer that points to a vertex having lowest value of the field
 * m_distanceFromRoot.  Remaining vertices are ordered according to 
 * increasing distance.
 *
 * @see SATSPFVertex
 * @param vNew The Shortest Path First Vertex to add to the queue.
 */
  void Push (SATSPFVertex *vNew);

/**
 * @brief Pop the Shortest Path First Vertex pointer at the top of the queue.
 *
 * The caller is given the responsibility for releasing the resources 
 * associated with the vertex.
 *
 * @see SATSPFVertex
 * @see Top ()
 * @returns The Shortest Path First Vertex pointer at the top of the queue.
 */
  SATSPFVertex* Pop (void);

/**
 * @brief Return the Shortest Path First Vertex pointer at the top of the 
 * queue.
 *
 * This method does not pop the SATSPFVertex* off of the queue, it simply 
 * returns the pointer.
 *
 * @see SATSPFVertex
 * @see Pop ()
 * @returns The Shortest Path First Vertex pointer at the top of the queue.
 */
  SATSPFVertex* Top (void) const;

/**
 * @brief Test the Candidate Queue to determine if it is empty.
 *
 * @returns True if the queue is empty, false otherwise.
 */
  bool Empty (void) const;

/**
 * @brief Return the number of Shortest Path First Vertex pointers presently
 * stored in the Candidate Queue.
 *
 * @see SATSPFVertex
 * @returns The number of SATSPFVertex* pointers in the Candidate Queue.
 */
  uint32_t Size (void) const;

/**
 * @brief Searches the Candidate Queue for a Shortest Path First Vertex 
 * pointer that points to a vertex having the given IP address.
 *
 * @see SATSPFVertex
 * @param addr The IP address to search for.
 * @returns The SATSPFVertex* pointer corresponding to the given IP address.
 */
  SATSPFVertex* Find (const Ipv4Address addr) const;

/**
 * @brief Reorders the Candidate Queue according to the priority scheme.
 * 
 * On completion, the top of the queue will hold the Shortest Path First
 * Vertex pointer that points to a vertex having lowest value of the field
 * m_distanceFromRoot.  Remaining vertices are ordered according to 
 * increasing distance.
 *
 * This method is provided in case the values of m_distanceFromRoot change
 * during the routing calculations.
 *
 * @see SATSPFVertex
 */
  void Reorder (void);

private:
/**
 * Candidate Queue copy construction is disallowed (not implemented) to 
 * prevent the compiler from slipping in incorrect versions that don't
 * properly deal with deep copies.
 * \param sr object to copy
 */
  SatCandidateQueue (SatCandidateQueue& sr);

/**
 * Candidate Queue assignment operator is disallowed (not implemented) to
 * prevent the compiler from slipping in incorrect versions that don't
 * properly deal with deep copies.
 * \param sr object to assign
 * \return copied object
 */
  SatCandidateQueue& operator= (SatCandidateQueue& sr);
/**
 * \brief return true if v1 < v2
 *
 * SATSPFVertexes are added into the queue according to the ordering
 * defined by this method. If v1 should be popped before v2, this 
 * method return true; false otherwise
 *
 * \param v1 first operand
 * \param v2 second operand
 * \return True if v1 should be popped before v2; false otherwise
 */
  static bool CompareSATSPFVertex (const SATSPFVertex* v1, const SATSPFVertex* v2);

  typedef std::list<SATSPFVertex*> CandidateList_t; //!< container of SATSPFVertex pointers
  CandidateList_t m_candidates;  //!< SATSPFVertex candidates

  /**
   * \brief Stream insertion operator.
   *
   * \param os the reference to the output stream
   * \param q the SatCandidateQueue
   * \returns the reference to the output stream
   */
  friend std::ostream& operator<< (std::ostream& os, const SatCandidateQueue& q);
};

} // namespace ns3

#endif /* SAT_CANDIDATE_QUEUE_H */
