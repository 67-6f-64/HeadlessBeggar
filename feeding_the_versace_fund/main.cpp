#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#include <iostream>
#include <iomanip>
#include <algorithm>
#include <vector>
#include <iterator>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <sstream>
#include <fstream>

#include <stdio.h>
#include <winsock2.h>
#include <windows.h>
#include <windowsx.h>

#include "client.hpp"
#include "core.hpp"
#include "defer.hpp"
#include "crypto.hpp"
#include "resource.h"

#pragma comment(lib, "ws2_32.lib")

using namespace std;

enum TradeState {
  STATE_INACTIVE,
  STATE_INITIATED,
  STATE_PLAYER_JOINED,
  STATE_PLAYER_MADE_OFFER,
  STATE_PLAYER_ACCEPTED,
};

struct Trade {
  TradeState state = STATE_INACTIVE;
  u32 char_id; 
  string ign;
  u64 last_activity;
};

u64 current_time_in_ms() {
   FILETIME ft;
   GetSystemTimeAsFileTime(&ft);

   LARGE_INTEGER li;
   li.LowPart = ft.dwLowDateTime;
   li.HighPart = ft.dwHighDateTime;

   return li.QuadPart / 10000;
}

// instance
struct Inst {
  vector<string> logs;
  string profile_file;
  GameClient client;

  // profile
  string name;
  string username;
  string password;
  u8 world;
  u8 channel;
  string pic;
  string macid;
  string hwid;
  string server_ip;
  u16 server_port;
  unordered_set<string> players_seen;

  u32 char_id;
  u32 account_id;

  bool in_game;
  Trade trade; // current trade
  unordered_map<u32, string> players; 
  u32 mesos;
  string ign;
};

struct World {
  Inst instances[100];
  s32 n_instances;
  HWND wnd;
};

static World world;

string format_number(int n) {
  string num = to_string(n);
  int pos = (int)num.length() - 3;
  while (pos > 0) {
    num.insert(pos, ",");
    pos -= 3;
  }
  return num;
}

bool is_inst_selected(Inst *inst) {
  auto cbox = GetDlgItem(world.wnd, IDC_ACCOUNTS);
  return inst == (world.instances + ComboBox_GetCurSel(cbox));
};

auto log_inst = [&](Inst *inst, ccstr s) {
  string str(s);
  inst->logs.push_back(str);

  if (is_inst_selected(inst)) {
    auto lb = GetDlgItem(world.wnd, IDC_LOGS);
    SendMessage(lb, LB_ADDSTRING, 0, (LPARAM)s);
    SendMessage(lb, LB_SETCURSEL, inst->logs.size() - 1, 0);
  }
};

int run_inst(int instid) {
  auto inst = world.instances + instid;
  auto client = &inst->client;

  #define log(fmt, ...) log_inst(inst, debug_print(fmt, __VA_ARGS__))
  #define log_error(fmt, ...) log_inst(inst, debug_error(fmt, __VA_ARGS__))

  { // try to log in
    if (!client->init(inst->server_ip, inst->server_port)) {
      log_error("unable to connect to server.");
      return EXIT_FAILURE;
    }

    auto last_login_activity = current_time_in_ms();
    bool in_game = false;

    client->auth(inst->username, inst->password);

    while (client->connected && !in_game) {
      auto p = client->read_packet();
      if (p == NULL) {
        if (current_time_in_ms() - last_login_activity > 5000) {
          log_error("login attempt timed out (maybe credentials wrong, or we're banned)");
          return EXIT_FAILURE;
        }
        continue;
      }
      switch (p->read2()) {
      case OP_RECV_PING:
        client->pong();
        break;
      case OP_RECV_LOGIN_STATUS:
        switch ((LoginStatus)p->read1()) {
        case LOGIN_SUCCESS:
          p->read1();
          p->read4();
          inst->account_id = p->read4();
          client->show_world();
          break;
        case LOGIN_DOESNT_HAPPEN:     log_error("Login failed (reason unknown).");       return EXIT_FAILURE;
        case LOGIN_TEMP_BAN:          log_error("Login failed (account tempbanned).");   return EXIT_FAILURE;
        case LOGIN_PERM_BAN:          log_error("Login failed (account permabanned).");  return EXIT_FAILURE;
        case LOGIN_WRONG_PASSWORD:    log_error("Login failed (wrong password).");       return EXIT_FAILURE;
        case LOGIN_WRONG_USERNAME:    log_error("Login failed (wrong username).");       return EXIT_FAILURE;
        case LOGIN_SYSTEM_ERROR:      log_error("Login failed (server error)");          return EXIT_FAILURE;
        case LOGIN_ALREADY_LOGGED_IN: log_error("Login failed (already logged in)");     return EXIT_FAILURE;
        }
        break;
      case OP_RECV_SERVER_LIST:
        if (p->read1() == 0xff)
          break;
        log("Received server list.");
        last_login_activity = current_time_in_ms();

        client->select_world(inst->world);
        break;
      case OP_RECV_WORLD_INFO:
        client->select_channel(inst->world, inst->channel);
        log("Received world info.");
        last_login_activity = current_time_in_ms();
        break;
      case OP_RECV_CHAR_INFO:
        p->read2();
        inst->char_id = p->read4();

        log("Received character info (ID = 0x%x).", inst->char_id);
        last_login_activity = current_time_in_ms();

        client->select_char_with_pic(inst->char_id, inst->pic, inst->macid, inst->hwid);
        break;
      case OP_RECV_SERVER_INFO: {
        p->read2();

        stringstream ss;
        for (u32 i = 0; i < 4; i++) {
          ss << (int)p->read1();
          if (i < 3)
            ss << '.'; 
        }

        u16 port = p->read2();
        inst->char_id = p->read4();

        log("Connecting to channel (%s:%d)...", ss.str().c_str(), port);
        last_login_activity = current_time_in_ms();

        client->disconnect();
        if (!client->init(ss.str(), port)) {
          log_error("Unable to connect to game server.");
          return EXIT_FAILURE;
        }

        client->announce_logged_in(inst->char_id);

        in_game = true;
        log("Connected!");
        break;
      }
      }
    }
    if (!in_game)
      return EXIT_FAILURE;
  }

  inst->mesos = 0;
  inst->ign = "";
  inst->in_game = true;
  
  // look for trades in a loop.
  auto trade = &inst->trade;
  trade->state = STATE_INACTIVE;
  trade->ign = "";
  trade->last_activity = current_time_in_ms();

  while (client->connected) {
    // clear up to 50 packets from the packet queue
    Packet *p;
    for (u32 i = 0; i < 50 && (p = client->read_packet()) != NULL; i++) {
      switch (p->read2()) {
      case OP_RECV_WARP_TO_MAP: {
        p->read4();
        p->read1();
        bool isconnect = p->read1();
        p->read2();

        if (isconnect) {
          p->skip(12); // rng seeds
          p->skip(13); // random crap

          inst->ign = "";
          for (u32 i = 0; i < 13; i++) {
            char ch = p->read1();
            if (ch != 0)
              inst->ign += ch;
          }

          p->skip(77);
          if (p->read1()) // has linked name
            p->readstr();

          inst->mesos = p->read4();

          if (is_inst_selected(inst)) {
            SetDlgItemText(world.wnd, IDC_IGN, inst->ign.c_str());
            SetDlgItemText(world.wnd, IDC_MESOS, format_number(inst->mesos).c_str());
          }
        }
        break;
      }
      case OP_RECV_UPDATE_STATS:
        p->read1();
        if (p->read4() == 0x40000) { // mesos
          inst->mesos = p->read4();
          if (is_inst_selected(inst))
            SetDlgItemText(world.wnd, IDC_MESOS, format_number(inst->mesos).c_str());
        }
        break;
      case OP_RECV_TRADE: {
        switch (p->read1()) {
        case TRADE_MESOS:
          trade->last_activity = current_time_in_ms();
          p->read1();
          log("%s offered %s mesos.", trade->ign.c_str(), format_number(p->read4()).c_str());
          break;

        case TRADE_ITEM:
          trade->last_activity = current_time_in_ms();
          log("%s offered an item.", trade->ign.c_str());
          break;

        case TRADE_ACCEPTED:
          log("%s accepted the trade.", trade->ign.c_str());
          trade->state = STATE_PLAYER_ACCEPTED;
          client->submit_trade();
          trade->last_activity = current_time_in_ms();
          break;

        case TRADE_JOINED: {
          if (trade->state != STATE_INITIATED)
            break;
          trade->state = STATE_PLAYER_JOINED;

          // we've now "seen" the character
          inst->players_seen.insert(trade->ign);

          // save profile file
          ofstream file(inst->profile_file);
          file << "name = " << inst->name << endl;
          file << "username = " << inst->username << endl;
          file << "password = " << inst->password << endl;
          file << "world = " << (int)inst->world << endl;
          file << "channel = " << (int)inst->channel << endl;
          file << "pic = " << inst->pic << endl;
          file << "macid = " << inst->macid << endl;
          file << "hwid = " << inst->hwid << endl;
          file << "server_ip = " << inst->server_ip << endl;
          file << "server_port = " << inst->server_port << endl;
          file << endl;
          for (auto ign : inst->players_seen)
            file << ign << "\n";

          log("%s joined the trade.", trade->ign.c_str());
          Sleep(2000);
          client->send_trade_message("hi sorry to be annoying but could i please have mesos for armor and pots?");
          trade->last_activity = current_time_in_ms();
          break;
        }

        case TRADE_CHAT: {
          p->read1();
          auto side = p->read1(); // side
          auto msg = p->readstr();

          trade->last_activity = current_time_in_ms();
          log("> %s", msg.c_str());
          break;
        }

        case TRADE_DECLINED:
          log("%s declined the trade.", trade->ign.c_str());
          trade->state = STATE_INACTIVE;
          break;

        case TRADE_ENDED:
          p->read1();
          switch (p->read1()) {
          case 0x02: log("%s cancelled the trade.", trade->ign.c_str()); break;
          case 0x07: log("Trade finished successfully!");                break;
          default:   log("Trade ended.");                                break;
          }
          trade->state = STATE_INACTIVE;
          break;
        }
        break;
      }
      case OP_RECV_PING:
        client->pong();
        break;
      case OP_RECV_PLAYER_ENTERED: {
        auto char_id = p->read4();
        if (char_id == inst->char_id)
          break;
        p->read1();
        auto ign = p->readstr();
        if (inst->players_seen.find(ign) == inst->players_seen.end()) {
          p->read1();
          inst->players[char_id] = ign;
          if (is_inst_selected(inst))
            SetDlgItemText(world.wnd, IDC_PLAYERS, format_number((int)inst->players.size()).c_str());
        }
        break;
      }
      case OP_RECV_PLAYER_EXITED: {
        auto char_id = p->read4();
        auto it = inst->players.find(char_id);
        if (it != inst->players.end()) {
          auto ign = it->second;
          if (inst->players_seen.find(ign) != inst->players_seen.end()) {
            inst->players.erase(char_id);
            if (is_inst_selected(inst))
              SetDlgItemText(world.wnd, IDC_PLAYERS, format_number((int)inst->players.size()).c_str());
          }
        }
        break;
      }
      }
    }

    auto has_time_elapsed = [&](u32 ms) -> bool {
      return (current_time_in_ms() - trade->last_activity > ms);
    };

    auto cancel_trade_if_time_elapsed = [&](u32 ms) {
      if (has_time_elapsed(ms)) {
        log("Player was unresponsive for %ds, cancelling trade.", ms / 1000);
        trade->state = STATE_INACTIVE;
        client->cancel_trade(); 
      }
    };

    // make a decision based on current state of trade.
    switch (trade->state) {
    case STATE_INACTIVE: {
      if (inst->players.size() > 0) {
        auto it = std::next(std::begin(inst->players), rand() % inst->players.size());

        trade->state = STATE_INITIATED;
        trade->char_id = it->first;
        trade->ign = it->second;
        trade->last_activity = current_time_in_ms();

        log("---");
        log("Initiating trade with %s.", trade->ign.c_str());
        client->initiate_trade(trade->char_id);

        inst->players.erase(it);
      }
      break;
    }
    case STATE_INITIATED: // waiting for acceptance
      cancel_trade_if_time_elapsed(15000);
      break;
    case STATE_PLAYER_JOINED: // waiting for offer
      cancel_trade_if_time_elapsed(20000);
      break;
    case STATE_PLAYER_MADE_OFFER: // waiting for submission
      cancel_trade_if_time_elapsed(30000);
      break;
    case STATE_PLAYER_ACCEPTED: // waiting for server
      cancel_trade_if_time_elapsed(10000);
    }
  }

  // means client disconnected -- successful program should run forever.
  return EXIT_FAILURE;
}

/* super ghetto function to read our ghetto config file, which takes the format

username = aklasldkfh         // config map with each key = value on new line
password = klsajdhfladsf
(...other props...)
server_ip = 1.2.3.4
server_port = 6969

tiger                         // after an empty line, a list of players we've already seen
fangblade
blahblah
bitcoinlover
*/
bool read_config_into_inst(string path, Inst *inst) {
  ifstream file(path);
  if (!file.is_open())
    return false;

  unordered_map<string, string> config;

  auto skipwhite = [&]() {
    char c = 0;
    do { c = file.get(); } while (isspace(c));
    file.putback(c);
  };

  while (!file.eof()) {
    char c;

    string key;
    while (!file.eof()) {
      c = file.get();
      if (c == ' ')
        break;
      key += c;
    }

    skipwhite();
    c = file.get();
    if (c != '=')
      break;
    skipwhite();

    string value;
    while (!file.eof()) {
      c = file.get();
      if (c == '\r' || c == '\n') {
        if (c == '\r')
          if (file.get() != '\n')
            return false;
        break;
      }
      value += c;
    }

    config[key] = value;

    // break out on empty line
    c = file.get();
    bool done = (c == '\r' || c == '\n' || c == -1);
    if (c != -1)
      file.putback(c);
    if (done)
      break;
  }

  inst->profile_file = path;
  inst->name = config["name"];
  inst->username = config["username"];
  inst->password = config["password"];
  inst->world = stoi(config["world"]);
  inst->channel = stoi(config["channel"]);
  inst->pic = config["pic"];
  inst->macid = config["macid"];
  inst->hwid =  config["hwid"];
  inst->server_ip = config["server_ip"];
  inst->server_port = stoi(config["server_port"]);

  if (!file.eof()) {
    // skip over blank line
    string line;
    getline(file, line);

    while (!file.eof()) {
      getline(file, line);
      inst->players_seen.insert(line);
    }
    inst->players_seen.erase("");
  }

  return true;
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE, LPSTR, int) {
  WSADATA wsaData;
  auto err = WSAStartup(MAKEWORD(2, 2), &wsaData);
  if (err != NO_ERROR) {
    debug_error("WSAStartup fucked up: %d", err);
    return EXIT_FAILURE;
  }
  defer { WSACleanup(); };

  // =============
  // load profiles
  // =============

  WIN32_FIND_DATAA find_data;
  auto find = FindFirstFileA("profiles/*", &find_data);
  if (find == INVALID_HANDLE_VALUE)
    return EXIT_FAILURE;
  defer { FindClose(find); };

  do {
    if (!(find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
      auto path = string("profiles/") + find_data.cFileName;
      auto inst = world.instances + (world.n_instances++);
      read_config_into_inst(path, inst);
    }
  } while (FindNextFileA(find, &find_data));

  // ===============================
  // create thread for each instance
  // ===============================

  vector<HANDLE> thread_handles;

  for (u32 i = 0; i < world.n_instances; i++) {
    auto proc = [](LPVOID p) -> DWORD {
      int instid = (int)(intptr_t)p;
      auto inst = world.instances + instid;
      while (true) {
        run_inst(instid);
        log_inst(inst, "Client has disconnected, reconnecting in 10 seconds...");
        Sleep(10000);
      }
    };
    auto h = CreateThread(NULL, 0, proc, (LPVOID)(uintptr_t)i, 0, NULL);
    if (h == NULL) {
      debug_error("failed to create thread for %s", world.instances[i].username.c_str());
      continue;
    }
    thread_handles.push_back(h);
  }

  // ==========
  // run window
  // ==========

  auto window_cb = [](HWND wnd, UINT msg, WPARAM wparam, LPARAM lparam) -> INT_PTR {
    static HFONT font;

    auto fill_account_details = [](HWND wnd, Inst *inst) {
      SetDlgItemText(wnd, IDC_ACCOUNT_NAME, inst->name.c_str());

      if (inst->in_game) { 
        if (inst->mesos == 0)
          SetDlgItemText(wnd, IDC_MESOS, "???");
        else
          SetDlgItemText(wnd, IDC_MESOS, format_number(inst->mesos).c_str());
        SetDlgItemText(wnd, IDC_IGN, (inst->ign == "" ? "???" : inst->ign.c_str()));
        SetDlgItemText(wnd, IDC_PLAYERS, format_number((int)inst->players.size()).c_str());
      } else {
        SetDlgItemText(wnd, IDC_MESOS, "???");
        SetDlgItemText(wnd, IDC_IGN, "???");
        SetDlgItemText(wnd, IDC_PLAYERS, "???");
      }

      auto lb = GetDlgItem(wnd, IDC_LOGS);
      SendMessage(lb, LB_RESETCONTENT, 0, 0);
      for (auto it : inst->logs)
        SendMessage(lb, LB_ADDSTRING, 0, (LPARAM)it.c_str());
      SendMessage(lb, LB_SETCURSEL, inst->logs.size() - 1, 0);
    };

    switch (msg) {
    case WM_INITDIALOG: {
      world.wnd = wnd;

      font = CreateFont(15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, "Consolas");
      if (font == NULL) {
        EndDialog(wnd, EXIT_FAILURE);
        break;
      }
      SendMessage(GetDlgItem(wnd, IDC_LOGS), WM_SETFONT, (WPARAM)font, TRUE);

      auto cbox = GetDlgItem(wnd, IDC_ACCOUNTS);
      for (u32 i = 0; i < world.n_instances; i++)
        ComboBox_AddString(cbox, world.instances[i].name.c_str());
      ComboBox_SetCurSel(cbox, 0);
      fill_account_details(wnd, world.instances + 0);
      break;
    }
    case WM_COMMAND:
      switch (LOWORD(wparam)) {
      case IDC_CLEARLOGS: {
        auto inst = world.instances + ComboBox_GetCurSel(GetDlgItem(wnd, IDC_ACCOUNTS));
        inst->logs.clear();
        SendMessage(GetDlgItem(wnd, IDC_LOGS), LB_RESETCONTENT, 0, 0);
        break;
      }
      case IDC_ACCOUNTS:
        switch (HIWORD(wparam)) {
        case CBN_SELCHANGE:
          fill_account_details(wnd, world.instances + ComboBox_GetCurSel((HWND)lparam));
          break;
        }
        break;
      }
      break;
    case WM_CLOSE:
      if (font != NULL)
        DeleteObject(font);
      EndDialog(wnd, EXIT_SUCCESS);
      break;
    }
    return FALSE;
  };

  return (int)DialogBox(inst, MAKEINTRESOURCE(IDD_DIALOG1), NULL, window_cb);
}
