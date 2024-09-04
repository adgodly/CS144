#include "tcp_receiver.hh"
#include "wrapping_integers.hh"

using namespace std;

void TCPReceiver::receive( TCPSenderMessage message )
{
  // Your code here.
  if ( writer().has_error() ) {
    return;
  }
  if ( message.RST ) {
    reader().set_error();
    return;
  }
  if ( not zero_point_.has_value() ) {
    if ( !message.SYN ) {
      return;
    }
    zero_point_.emplace( message.seqno );
  }
  uint64_t checkpoint = writer().bytes_pushed() + 1; // SYN Flag
  uint64_t absolute_seqno = message.seqno.unwrap( zero_point_.value(), checkpoint );
  uint64_t stream_index = absolute_seqno + static_cast<uint64_t>( message.SYN ) - 1;

  reassembler_.insert( stream_index, message.payload, message.FIN );
}

TCPReceiverMessage TCPReceiver::send() const
{
  // Your code here.
  uint64_t capacity = reassembler_.writer().available_capacity();
  uint16_t window_size = capacity > UINT16_MAX ? UINT16_MAX : static_cast<uint16_t>( capacity );
  if ( zero_point_.has_value() ) {
    uint64_t ackno = writer().bytes_pushed() + 1 + static_cast<uint64_t>( writer().is_closed() );
    return { Wrap32::wrap( ackno, zero_point_.value() ), window_size, writer().has_error() };
  }
  return { nullopt, window_size, writer().has_error() };
}
