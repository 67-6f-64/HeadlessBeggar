#pragma once

#include <vector>
#include <sstream>
#include <string>
#include <iomanip>
using namespace std;

enum {
  OP_RECV_LOGIN_STATUS = 0x0000,
  OP_RECV_WORLD_INFO = 0x0003,
  OP_RECV_SERVER_LIST = 0x000a,
  OP_RECV_CHAR_INFO = 0x000b,
  OP_RECV_SERVER_INFO = 0x000c,
  OP_RECV_PING = 0x0011,
  OP_RECV_PLAYER_ENTERED = 0x00a0,
  OP_RECV_PLAYER_EXITED = 0x00a1,
  OP_RECV_TRADE = 0x013a,
  OP_RECV_UPDATE_STATS = 0x001f,
  OP_RECV_WARP_TO_MAP = 0x007d,
  /**/
  OP_SEND_LOGIN = 0x0001,
  OP_SEND_SELECT_CHANNEL = 0x0005,
  OP_SEND_SELECT_WORLD = 0x0006,
  OP_SEND_SHOW_WORLD = 0x000b,
  OP_SEND_ANNOUNCE_LOGGED_IN = 0x0014,
  OP_SEND_PONG = 0x0018,
  OP_SEND_TRADE = 0x007b,
  OP_SEND_SELECT_CHAR_WITH_PIC = 0x001e,
};

enum LoginStatus {
  LOGIN_SUCCESS = 0,
  LOGIN_DOESNT_HAPPEN = 1, // ???
  LOGIN_TEMP_BAN = 2,
  LOGIN_PERM_BAN = 3,
  LOGIN_WRONG_PASSWORD = 4,
  LOGIN_WRONG_USERNAME = 5,
  LOGIN_SYSTEM_ERROR = 6,
  LOGIN_ALREADY_LOGGED_IN = 7,
};

enum TradeOp {
  TRADE_MESOS = 0x10,
  TRADE_ITEM = 0x0f,
  TRADE_JOINED = 0x04,
  TRADE_CHAT = 0x06,
  TRADE_ACCEPTED = 0x11,
  TRADE_ENDED = 0x0a,
  TRADE_DECLINED = 0x03,
};

struct Packet {
  vector<u8> bytes;
  s32 i = 0;

  void clear() {
    bytes.clear();
    i = 0;
  }

  void add1(u8 x) {
    bytes.push_back(x);
  }

  void add2(u16 x) {
    auto ptr = (u8*)(&x);
    for (u32 i = 0; i < 2; i++)
      add1(ptr[i]);
  }

  void add4(u32 x) {
    auto ptr = (u8*)(&x);
    for (u32 i = 0; i < 4; i++)
      add1(ptr[i]);
  }

  void addstr(string s) {
    add2((u16)s.length());
    for (char it : s)
      add1(it);
  }

  bool end() {
    return (i >= bytes.size());
  }

  u8 read1() {
    if (end())
      return 0;
    return bytes[i++];
  }

  u16 read2() {
    u16 ret;
    auto ptr = (u8*)(&ret);
    for (u32 i = 0; i < 2; i++)
      ptr[i] = read1();
    return ret;
  }

  u32 read4() {
    u32 ret;
    auto ptr = (u8*)(&ret);
    for (u32 i = 0; i < 4; i++)
      ptr[i] = read1();
    return ret;
  }

  string readstr() {
    auto len = read2();
    string ret(len, ' ');
    for (u32 i = 0; i < len; i++)
      ret[i] = read1();
    return ret;
  }

  void skip(u32 n) {
    for (u32 i = 0; i < n; i++)
      read1();
  }

  void print(bool recv) {
    stringstream ss;
    for (auto byte : bytes)
      ss << std::hex << std::setfill('0') << std::setw(2) << (int)byte << " ";
    printf("[%s] %s\n", recv ? "recv" : "send", ss.str().c_str());
  }
};

