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
 */

#ifndef SATCOMPUTE_FNV1A64_H
#define SATCOMPUTE_FNV1A64_H

#include <array>
#include <cstdint>

namespace ns3 {

inline uint64_t
Fnv1a64(const std::array<uint8_t, 21>& bytes)
{
  static const uint64_t offsetBasis = 14695981039346656037ULL;
  static const uint64_t prime = 1099511628211ULL;

  uint64_t hash = offsetBasis;
  for (uint8_t byte : bytes)
    {
      hash ^= byte;
      hash *= prime;
    }
  return hash;
}

} // namespace ns3

#endif
