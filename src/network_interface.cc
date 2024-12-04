#include <iostream>

#include "arp_message.hh"
#include "exception.hh"
#include "network_interface.hh"

using namespace std;

//! \param[in] ethernet_address Ethernet (what ARP calls "hardware") address of the interface
//! \param[in] ip_address IP (what ARP calls "protocol") address of the interface
NetworkInterface::NetworkInterface( string_view name,
                                    shared_ptr<OutputPort> port,
                                    const EthernetAddress& ethernet_address,
                                    const Address& ip_address )
  : name_( name )
  , port_( notnull( "OutputPort", move( port ) ) )
  , ethernet_address_( ethernet_address )
  , ip_address_( ip_address )
{
  cerr << "DEBUG: Network interface has Ethernet address " << to_string( ethernet_address ) << " and IP address "
       << ip_address.ip() << "\n";
}

//! \param[in] dgram the IPv4 datagram to be sent
//! \param[in] next_hop the IP address of the interface to send it to (typically a router or default gateway, but
//! may also be another host if directly connected to the same network as the destination) Note: the Address type
//! can be converted to a uint32_t (raw 32-bit IP address) by using the Address::ipv4_numeric() method.
void NetworkInterface::send_datagram( const InternetDatagram& dgram, const Address& next_hop )
{
  // Your code here.
  // 如果next_hop在路由表中存在条目

  if ( ARP_table_.find( next_hop.ipv4_numeric() ) != ARP_table_.end() ) {
    // cout << "hhh" << endl;
    auto it = ARP_table_.find( next_hop.ipv4_numeric() );
    EthernetHeader header_;
    header_.type = EthernetHeader::TYPE_IPv4;
    header_.dst = it->second.eth_add_;
    header_.src = ethernet_address_;
    EthernetFrame frame_;
    frame_.header = header_;
    frame_.payload = serialize( dgram );
    transmit( frame_ );
    return;
  }

  // 如果不再ARP表中
  // 如果已经5s内已经发送过某个IP的ARP申请，则不发送了
  else {
    if ( IP_Request.find( next_hop.ipv4_numeric() ) != IP_Request.end() )
      return;
    IP_Request.insert( { next_hop.ipv4_numeric(), 0 } );
    ARPMessage arp_mes_;
    arp_mes_.opcode = ARPMessage::OPCODE_REQUEST;
    arp_mes_.sender_ethernet_address = ethernet_address_;
    arp_mes_.sender_ip_address = ip_address_.ipv4_numeric();
    arp_mes_.target_ip_address = next_hop.ipv4_numeric();
    // arp_mes_.target_ethernet_address = ETHERNET_BROADCAST;
    datagrams_to_be_sent_.push_back( { dgram, next_hop } );

    EthernetHeader header_;
    header_.dst = ETHERNET_BROADCAST;
    header_.src = ethernet_address_;
    header_.type = EthernetHeader::TYPE_ARP;

    EthernetFrame frame_;
    frame_.header = header_;
    zai frame_.payload = serialize( arp_mes_ );
    transmit( frame_ );
    return;
  }
}

//! \param[in] frame the incoming Ethernet frame
void NetworkInterface::recv_frame( const EthernetFrame& frame )
{
  // Your code here.
  // 如果是IPV4类型的数据帧
  if ( frame.header.dst != ETHERNET_BROADCAST && frame.header.dst != ethernet_address_ ) {
    return;
  }
  if ( frame.header.type == EthernetHeader::TYPE_IPv4 ) {
    InternetDatagram Dgram;
    if ( !parse( Dgram, frame.payload ) ) {
      return;
    }
    // 把frame的负载部分解析成一个数据报文
    datagrams_received_.push( Dgram );
    return;
  }
  if ( frame.header.type == EthernetHeader::TYPE_ARP ) {
    ARPMessage arp_mes_;
    if ( parse( arp_mes_, frame.payload ) == false ) {
      return;
    }
    ARP_table_.insert( { arp_mes_.sender_ip_address, { arp_mes_.sender_ethernet_address, 0 } } );
    // 这是个arp请求
    if ( arp_mes_.opcode == ARPMessage::OPCODE_REQUEST ) {
      // Address source_ip_ = Address::from_ipv4_numeric( arp_mes_.sender_ip_address );

      auto it = IP_Request.find( arp_mes_.sender_ip_address );
      if ( it != IP_Request.end() ) {
        IP_Request.erase( it );
      }
      // 如果该arp请求的目标ip为本接口
      if ( arp_mes_.target_ip_address == ip_address_.ipv4_numeric() ) {
        // 需要返回一个arp回复
        EthernetHeader header_;

        header_.dst = arp_mes_.sender_ethernet_address;
        header_.src = ethernet_address_;
        header_.type = EthernetHeader::TYPE_ARP;

        ARPMessage res_arp_;
        res_arp_.opcode = ARPMessage::OPCODE_REPLY;
        res_arp_.sender_ethernet_address = ethernet_address_;
        res_arp_.sender_ip_address = ip_address_.ipv4_numeric();
        res_arp_.target_ip_address = arp_mes_.sender_ip_address;
        res_arp_.target_ethernet_address = arp_mes_.sender_ethernet_address;

        EthernetFrame frame_;
        frame_.header = header_;
        frame_.payload = serialize( res_arp_ );

        transmit( frame_ );
      }
      return;
    }
    if ( arp_mes_.target_ethernet_address == ethernet_address_ ) {
      // 这是个arp回复
      // 需要在arp表中记录下
      // Address source_ip_ = Address::from_ipv4_numeric( arp_mes_.sender_ip_address );
      // ARP_table_.insert( { source_ip_.ipv4_numeric(), { arp_mes_.sender_ethernet_address, 0 } } );
      // 收到回复后，需要尝试清除下datagram_to_be_sent
      for ( auto i = datagrams_to_be_sent_.begin(); i != datagrams_to_be_sent_.end(); ) {
        Datagram_buffer d = *i;
        if ( d.ip_add.ipv4_numeric() == arp_mes_.sender_ip_address ) {
          EthernetHeader header;
          header.type = EthernetHeader::TYPE_IPv4;
          header.dst = arp_mes_.sender_ethernet_address;
          header.src = ethernet_address_;

          EthernetFrame f;
          f.header = header;
          f.payload = serialize( d.dgram );
          transmit( f );
          i = datagrams_to_be_sent_.erase( i );
        } else {
          i++;
        }
      }
    }
    //
  }
}
//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void NetworkInterface::tick( const size_t ms_since_last_tick )
{
  // Your code here.
  for ( auto it = IP_Request.begin(); it != IP_Request.end(); ) {
    it->second += ms_since_last_tick;
    if ( it->second >= 5000 )
      it = IP_Request.erase( it );
    else
      it++;
  }

  // check the time of the mapping between IP address and Ethernet address
  for ( auto it = ARP_table_.begin(); it != ARP_table_.end(); ) {
    ( it->second ).time += ms_since_last_tick;
    if ( ( it->second ).time >= 30000 )
      it = ARP_table_.erase( it );
    else
      it++;
  }
}
