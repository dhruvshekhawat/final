#include <vector>
#include <map>
#include "sequence-number.h"

#ifndef TCP_TYPEDEFS_H
#define TCP_TYPEDEFS_H

namespace ns3 {

typedef enum { MAX_FLAGS = 0x40 } TCPMaxFlags_t;  

typedef enum {
  CLOSED,       // 0
  LISTEN,       // 1
  SYN_SENT,     // 2
  SYN_RCVD,     // 3
  ESTABLISHED,  // 4
  CLOSE_WAIT,   // 5
  LAST_ACK,     // 6
  FIN_WAIT_1,   // 7
  FIN_WAIT_2,   // 8
  CLOSING,      // 9
  TIME_WAIT,   // 10      
RECEIVED_ACK,  //11
RECEIVED_NEW_ACK, //12
DATA_RECEIVED,  //13
CHECK,         //14
PERSIST_STATE,  //15
BUFFER_READ,    //16
SEND,           //17
SCHEDULE_ACK,//18
CHECK_REC,//19
UPDATE_RETX,//20
IDLE_S,//21
IDLE_R,//22
LAST_STATE} States_t; //23

typedef enum {
  APP_LISTEN,   // 0
  APP_CONNECT,  // 1
  APP_SEND,     // 2     
  APP_CLOSE,    // 3
  TIMEOUT,      // 4
  ACK_RX,       // 5
  SYN_RX,       // 6
  SYN_ACK_RX,   // 7
  FIN_RX,       // 8
  FIN_ACK_RX,   // 9
  FIN_ACKED,    // 10
  RST_RX,       // 11
  BAD_FLAGS,    // 12
RWIND_0,        //13
FORCED_TRANS,//14
CHECK_NAGLE,//15
SEND_DATA,//16
NOTIFY_APP, //17
CHECK_RX_BUFFER,//18
ACK_CHECK,//19
SET_RTO,//20
RX_BUFFER_FULL,//21
RX_BUFFER_GAP,//22
DELAY_ACK_PERMIT, //23
DATA_RECV,//24
LAST_DATA_FIN,//25
SEND_SYN_ACK,//26
LAST_EVENT} Events_t;//27

typedef enum {
  NO_ACT,       // 0
  ACK_TX,       // 1
  ACK_TX_1,     // 2 - ACK response to syn
  RST_TX,       // 3
  SYN_TX,       // 4
  SYN_ACK_TX,   // 5
  SYN_ACK_TX1,  //6
  FIN_TX,       // 7
  FIN_ACK_TX,   // 8
  GOT_ACK,      // 9
  TX_DATA,      // 10
  PEER_CLOSE,   // 11
  APP_CLOSED,   // 12
  CANCEL_TM,    // 13
  SERV_NOTIFY,  // 14
  SS_CWND_1,//15
SS_CWND_2,//16
  RWIND_0_ACTION,//17
  BUFFER_READ_ACTIONS,//18
  NAGLE_ACTION,//19
  SEND_DATA_ACTION,//20
RX_BUFFER_ACTIONS,//21
NOTIFY_APP_ACTION,//22
DATA_RECEIVED_ACTION,//23
ACK_CHECK_ACTION,//24
SET_RTO_ACTION,//25
FORK_DATA_SOCKET,//26
SET_DELAYED_ACK,//27
TX_DATA_ARQ,//28
LAST_ACTION } Actions_t;//29

class State_Action  // State/Action pair
{
public:
  State_Action () : state (LAST_STATE), action (LAST_ACTION) { }
  State_Action (States_t s, Actions_t a) : state (s), action (a) { }
public:
  States_t  state;
  Actions_t action;
};
typedef std::vector<State_Action>  StateActionVector_t;
typedef std::vector<StateActionVector_t> StateActions_t;  // One per current state
typedef std::vector<Events_t> EventVector_t;      // For flag events lookup

class ExtendedFiniteStateMachine {
  public:
    ExtendedFiniteStateMachine ();
    State_Action Table_Lookup (States_t, Events_t);
    Events_t FlagsEvent (uint8_t); // Lookup event from flags

  public:
    StateActions_t act_Table; // Action table
    EventVector_t     Flag_EV; // Flags event lookup  
};

}//namespace ns3
#endif //TCP_TYPEDEFS_H
