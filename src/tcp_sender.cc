#include "tcp_sender.hh"
#include "tcp_config.hh"

using namespace std;

RetransmissionTimer& RetransmissionTimer::active() noexcept
{
  is_active_ = true;
  return *this;
}
RetransmissionTimer& RetransmissionTimer::Reset() noexcept
{
  time_passed_ = 0;
  return *this;
}
RetransmissionTimer& RetransmissionTimer::tick( uint64_t ms ) noexcept
{
  time_passed_ += is_active_ ? ms : 0;
  return *this;
}
RetransmissionTimer& RetransmissionTimer::Timeout() noexcept
{
  RTO_ <<= 1;
  return *this;
}
uint64_t TCPSender::sequence_numbers_in_flight() const
{
  return numbers_in_flight_;
}

uint64_t TCPSender::consecutive_retransmissions() const
{
  // Your code here.
  return RetranmissionCnt_;
}

void TCPSender::push( const TransmitFunction& transmit )
{
  // 从流中读取数据
  Reader& bytes_reader = input_.reader();
  const size_t window_size = wnd_size_ == 0 ? 1 : wnd_size_;
  FIN_ |= bytes_reader.is_finished();
  if ( SENT_FIN_ )
    return; // 如果已经发送了FIN，TCP连接就结束了
  for ( string payload {}; numbers_in_flight_ < window_size && !SENT_FIN_; payload.clear() ) {
    string_view bytes_view = bytes_reader.peek();
    if ( bytes_view.empty() && SENT_SYN_ && !FIN_ )
      break;
    while ( payload.size() + numbers_in_flight_ + ( !SENT_SYN_ ) < window_size
            && payload.size() < TCPConfig::MAX_PAYLOAD_SIZE ) {
      if ( bytes_view.empty() || FIN_ ) {
        break;
      }
      const uint64_t available_size
        = min( TCPConfig::MAX_PAYLOAD_SIZE - payload.size(),
               window_size
                 - ( payload.size() + numbers_in_flight_
                     + !SENT_SYN_ ) ); // 如果已經發送了SYN，則不需要預劉位置，FIN不需要考慮
      if ( bytes_view.size() > available_size ) {
        bytes_view.remove_suffix( bytes_view.size() - available_size );
      }
      payload.append( bytes_view );
      bytes_reader.pop( bytes_view.size() );
      FIN_ |= bytes_reader.is_finished(); // 每次都要檢查
      bytes_view = bytes_reader.peek();
    }
    auto& msg
      = outgoing_bytes_.emplace( make_message( next_seq_, move( payload ), SENT_SYN_ ? SYN_ : true, FIN_ ) );
    const size_t margin = SENT_SYN_ ? SYN_ : 0;
    // FIN位是否可以在本報文中發送出去
    if ( FIN_ && ( msg.sequence_length() - margin ) + numbers_in_flight_ > window_size ) {
      msg.FIN = false;
    } else if ( FIN_ ) {
      SENT_FIN_ = true;
    }
    // 只有第一個報文需要-1
    uint64_t message_size = msg.sequence_length() - margin;
    numbers_in_flight_ += message_size;
    next_seq_ += message_size; // 已经发送了next_seq_字节
    SENT_SYN_ = true;          // 只要发送过一次报文，SYN就已经发送出去了
    transmit( msg );
    // 启动计时器
    if ( message_size != 0 )
      timer_.active();
  }
}

TCPSenderMessage TCPSender::make_empty_message() const
{
  // Your code here.
  return make_message( next_seq_, {}, false, false );
}
TCPSenderMessage TCPSender::make_message( uint64_t seqno, string payload, bool SYN, bool FIN ) const
{
  return { .seqno = Wrap32::wrap( seqno, isn_ ),
           .SYN = SYN,
           .payload = move( payload ),
           .FIN = FIN,
           .RST = input_.reader().has_error() };
}
void TCPSender::receive( const TCPReceiverMessage& msg )
{
  // Your code here.
  wnd_size_ = msg.window_size;
  if ( !msg.ackno.has_value() ) {
    if ( msg.window_size == 0 )
      input_.set_error();
    return;
  }
  uint64_t excepting_seqno = msg.ackno->unwrap( isn_, next_seq_ );
  if ( excepting_seqno > next_seq_ ) // 确认号大于当前已经发送字节的序号
    return;
  // 确认号小于等于当前字节
  bool has_ack = false;
  while ( !outgoing_bytes_.empty() ) {
    auto MsgFromBuffer_ = outgoing_bytes_.front();
    const uint64_t final_seqno = ack_seq_ + MsgFromBuffer_.sequence_length() - MsgFromBuffer_.SYN;
    // 对首报文只有部分被确认，或者没被确认
    if ( excepting_seqno <= ack_seq_ || excepting_seqno < final_seqno )
      break;
    has_ack = true;
    numbers_in_flight_ -= MsgFromBuffer_.sequence_length() - SYN_;
    ack_seq_ += MsgFromBuffer_.sequence_length() - SYN_;
    SYN_ = SENT_SYN_ ? SYN_ : excepting_seqno <= next_seq_; // 疑似所有报文的SYN都是1
    outgoing_bytes_.pop();
  }
  // 如果有报文被确认
  if ( has_ack ) {
    if ( outgoing_bytes_.empty() ) {
      // 所有分组都被确认,关闭计时器
      timer_ = RetransmissionTimer( initial_RTO_ms_ );
    } else
      // 重启计时器
      timer_ = move( RetransmissionTimer( initial_RTO_ms_ ).active() );
    RetranmissionCnt_ = 0;
  }
}

void TCPSender::tick( uint64_t ms_since_last_tick, const TransmitFunction& transmit )
{
  // Your code here.
  // 超时
  if ( timer_.tick( ms_since_last_tick ).is_expired() ) {
    transmit( outgoing_bytes_.front() ); // 重传队首元素，因为计时器是队首元素的
    if ( wnd_size_ == 0 )
      timer_.Reset();
    else
      timer_.Timeout().Reset();
    RetranmissionCnt_++;
  }
}
