#include "dtag.h"

#include "ns3/log.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("DTag");


NS_OBJECT_ENSURE_REGISTERED (DTag);

TypeId
DTag::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::DTag")
    .SetParent<Tag> ()
    .SetGroupName("Internet")
    .AddConstructor<DTag> ()
  ;
  return tid;
}

DTag::DTag ()
  : Tag (),
    m_timestamp (),
    m_hops (0),
    m_prio (0),
    m_tag (0)
{
  NS_LOG_FUNCTION_NOARGS ();
}

DTag::~DTag ()
{
  NS_LOG_FUNCTION_NOARGS ();
}

TypeId
DTag::GetInstanceTypeId (void) const
{
  return GetTypeId ();
}

uint32_t
DTag::GetSerializedSize (void) const
{
  NS_LOG_FUNCTION (this);
  ///\todo update size when add new member
  return 
        sizeof (double)
      + sizeof (m_hops)
      + sizeof (m_prio)
      + sizeof (m_size)
      + sizeof (m_tag)
      ;
}

void
DTag::Serialize (TagBuffer buf) const
{
  NS_LOG_FUNCTION (this << &buf);
  buf.WriteDouble (m_timestamp.GetSeconds ());
  buf.WriteU32 (m_hops);
  buf.WriteU32 (m_prio);
  buf.WriteU32 (m_size);
  buf.WriteU32 (m_tag);
}

void
DTag::Deserialize (TagBuffer buf)
{
  NS_LOG_FUNCTION (this << &buf);
  m_timestamp = Time::FromDouble (buf.ReadDouble (), Time::S);
  m_hops = buf.ReadU32 ();
  m_prio = buf.ReadU32 ();
  m_size = buf.ReadU32 ();
  m_tag = buf.ReadU32 ();
  //m_curqlen = buf.ReadU32 ();
}

void
DTag::Print (std::ostream &os) const
{
  NS_LOG_FUNCTION (this << &os);
  os << "PKT INFO [size: " << m_size;
  os << ", prio: " << m_prio;
  os << ", Timestamp: " << m_timestamp;
  os << ", hops: " << m_hops;
  os << ", tag: " << m_tag;
  os << "] " << std::endl;
}

void 
DTag::SetTimestamp (Time timestamp)
{
  NS_LOG_FUNCTION (this << timestamp);
  m_timestamp = timestamp; 
}


Time 
DTag::GetTimestamp (void) const
{
  return m_timestamp;
}


void
DTag::HopAdd ()
{
  m_hops ++;
}


uint32_t 
DTag::GetHops (void) const
{
  return m_hops;
}

void DTag::SetPrio (uint32_t prio){
    m_prio = prio;
}

uint32_t DTag::GetPrio (void)  const
{
    return m_prio;
}

void DTag::SetSize (uint32_t size){
  m_size = size;
}

uint32_t DTag::GetSize (void)  const{
  return m_size;
}

void DTag::SetTag (uint32_t tag){
  m_tag = tag;
}

uint32_t DTag::GetTag (void)  const{
  return m_tag;
}

bool
DTag::operator == (const DTag &other) const
{
  return 
       m_prio == other.m_prio
       && m_size == other.m_size
       && m_tag == other.m_tag
      ;
}

bool
DTag::operator != (const DTag &other) const
{
  return !operator == (other);
}

} //namespace ns3