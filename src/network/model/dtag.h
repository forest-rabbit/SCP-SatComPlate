#ifndef DTAG_H
#define DTAG_H

#include <stdint.h>

#include "ns3/tag.h"
#include "ns3/nstime.h"
#include "ns3/simulator.h"

namespace ns3 {

class DTag : public Tag
{
public:
  /**
   * \brief Get the type ID.
   * \return the object TypeId
   */
  static TypeId GetTypeId (void);

  DTag ();
  ~DTag ();

  // inherited from Tag
  virtual TypeId GetInstanceTypeId (void) const;
  virtual uint32_t GetSerializedSize (void) const;
  virtual void Serialize (TagBuffer buf) const;
  virtual void Deserialize (TagBuffer buf);
  virtual void Print (std::ostream &os) const;


  void SetTimestamp (Time timestamp);
  Time GetTimestamp (void)  const;

  void HopAdd();
  uint32_t GetHops (void) const;

  void SetPrio (uint32_t prio);
  uint32_t GetPrio (void)  const;

  void SetSize (uint32_t size);
  uint32_t GetSize (void)  const;

  void SetTag (uint32_t tag);
  uint32_t GetTag (void)  const;

  bool operator == (const DTag &other) const;
  bool operator != (const DTag &other) const;

private:
  // param for delaytag
  Time m_timestamp;      //!< current packet enqueue timestamp
  uint32_t m_hops;    //!< 
  uint32_t m_prio;    //!< pkt优先级，0表示控制信息，1表示业务数据
  uint32_t m_size;    //!< pkt size
  uint32_t m_tag;     //!< 控制报文tag，区分是主控制器-从控制器， 从控制器-从控制器， 从控制器-普通卫星之间的消息类型
};

} //namespace ns3

#endif // Qdcn_TAG_H