#pragma once

#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#include <string>

#include "core.hpp"
#include "packet.hpp"
#include "crypto.hpp"

using namespace std;
#pragma comment(lib, "ws2_32.lib")

struct GameClient {
  SOCKET conn;
  bool connected;
  Packet packet;

  u16 major_version;
  string minor_version;
  u8 iv_send[4];
  u8 iv_recv[4];
  u8 game_locale;

  bool init(string ip, u16 port) {
    connected = false;

    conn = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (conn == INVALID_SOCKET) {
      debug_error("failed to open socket: %ld\n", WSAGetLastError());
      return false;
    }

    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(ip.c_str());
    addr.sin_port = htons(port);

    auto err = ::connect(conn, (SOCKADDR*)&addr, sizeof(addr));
    if (err == SOCKET_ERROR) {
      debug_error("connect failed: %ld\n", WSAGetLastError()); 
      return false;
    }

    // read handshake

    u16 len = 0, slen = 0;
    u8 *tmp = NULL;

    force_read(&len, sizeof(len));
    force_read(&major_version, sizeof(major_version));
    force_read(&slen, sizeof(slen));

    tmp = new u8[slen];
    defer { delete[] tmp; };

    force_read(tmp, slen);
    minor_version = string(tmp, tmp + slen);
    force_read(iv_send, 4);
    force_read(iv_recv, 4);
    force_read(&game_locale, sizeof(game_locale));

    debug_print("major_version = %d", major_version);
    debug_print("minor_version = %s", minor_version.c_str());
    debug_print("iv_send = %02x %02x %02x %02x", iv_send[0], iv_send[1], iv_send[2], iv_send[3]);
    debug_print("iv_recv = %02x %02x %02x %02x", iv_recv[0], iv_recv[1], iv_recv[2], iv_recv[3]);
    debug_print("game_locale = %d", game_locale);

    connected = true;
    return true;
  }

  void disconnect() { 
    closesocket(conn);
    connected = false;
  }

  bool force_send(void *buf, s32 len) {
    int offset = 0;
    do {
      int n = send(conn, (char*)buf + offset, (int)len, 0);
      if (n == 0 || n == SOCKET_ERROR) {
        debug_error("server disconnected while we tried to send something.");
        disconnect();
        return false;
      }
      len -= n;
      offset += n;
    } while (len > 0);
    return true;
  }

  bool force_read(void *buf, s32 len) {
    int n = recv(conn, (char*)buf, (int)len, MSG_WAITALL);
    return (n != 0 && n != SOCKET_ERROR);
  }

  Packet *read_packet() {
    u8 lenbuf[4];
    if (recv(conn, (char*)lenbuf, 4, MSG_PEEK) < 4)
      return NULL;

    recv(conn, (char*)lenbuf, 4, 0);

    auto len = crypto::get_packet_length(lenbuf);
    if (len < 2) {
      disconnect();
      return NULL;
    }

    auto buf = new u8[len];
    defer { delete[] buf; };

    if (!force_read(buf, len)) {
      debug_error("connection closed while trying to read");
      disconnect();
      return NULL;
    }

    crypto::decrypt(buf, iv_recv, len);

    packet.bytes.assign(buf, buf+len);
    packet.i = 0;
    // packet.print(true);

    return &packet;
  }

  void send_packet(Packet *p) {
    // p->print(false);

    u16 len = (u16)p->bytes.size();
    auto tmp = new u8[len + 4];
    defer { delete[] tmp; };

    copy(p->bytes.begin(), p->bytes.begin() + len, tmp + 4);
    crypto::create_packet_header(tmp, iv_send, len, major_version);
    crypto::encrypt(tmp + 4, iv_send, len);

    force_send(tmp, len + 4);
  }

  // ====================
  // packet builders
  // ====================

  void submit_trade() {
    Packet p1;
    p1.add2(OP_SEND_TRADE);
    p1.add2(0x0014);
    send_packet(&p1);

    Packet p2;
    p2.add2(OP_SEND_TRADE);
    p2.add2(0x0011);
    send_packet(&p2);
  }

  void send_trade_message(string s) {
    Packet p;
    p.add2(OP_SEND_TRADE);
    p.add1(0x06);
    p.addstr(s);
    send_packet(&p);
  }

  void cancel_trade() {
    Packet p;
    p.add2(OP_SEND_TRADE);
    p.add1(0x0a);
    send_packet(&p);
  }

  void initiate_trade(u32 char_id) {
    Packet p1;
    p1.add2(OP_SEND_TRADE);
    p1.add1(0x00);
    p1.add1(0x03);
    p1.add1(0x00);
    send_packet(&p1);

    Packet p2;
    p2.add2(OP_SEND_TRADE);
    p2.add1(0x02);
    p2.add4(char_id);
    send_packet(&p2);
  }

  void auth(string username, string password) {
    Packet p;
    p.add2(OP_SEND_LOGIN);
    p.addstr(username);
    p.addstr(password);

    for (u32 i = 0; i < 6; i++)
      p.add1(0);

    p.add4(0xf656e56d); // ???
    p.add4(0); // ???
    p.add2(0x1ce8);
    p.add4(0); // ???
    p.add1(2); // ???

    for (u32 i = 0; i < 6; i++)
      p.add1(0);

    send_packet(&p);
  }

  void select_channel(u8 world, u8 channel) {
    Packet p;
    p.add2(OP_SEND_SELECT_CHANNEL);
    p.add1(2);
    p.add1(world);
    p.add1(channel);
    p.add4(0x2e00a8c0); // ???
    send_packet(&p);
  }

  void select_world(u8 world) {
    Packet p;
    p.add2(OP_SEND_SELECT_WORLD);
    p.add2(world);
    send_packet(&p);
  }

  void select_char_with_pic(u32 charid, string pic, string macid, string hwid) {
    Packet p;
    p.add2(OP_SEND_SELECT_CHAR_WITH_PIC);
    p.addstr(pic);
    p.add4(charid);
    p.addstr(macid);
    p.addstr(hwid);
    send_packet(&p);
  }

  void pong() {
    Packet p;
    p.add2(OP_SEND_PONG);
    send_packet(&p);
  }

  void show_world() {
    Packet p;
    p.add2(OP_SEND_SHOW_WORLD);
    send_packet(&p);
  }

  void announce_logged_in(u32 charid) {
    Packet p;
    p.add2(OP_SEND_ANNOUNCE_LOGGED_IN);
    p.add4(charid);
    p.add2(0);
    send_packet(&p);
  }
};
