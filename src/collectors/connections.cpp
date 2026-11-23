#include "collectors/connections.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <tcpmib.h>
#include <udpmib.h>
#include <tlhelp32.h>

#include "core/log.h"

#pragma comment(lib, "iphlpapi.lib")

namespace collectors {
namespace {

std::wstring format_addr(uint32_t addr_be) {
  wchar_t buf[64]{};
  in_addr a{};
  a.S_un.S_addr = addr_be;
  if (InetNtopW(AF_INET, &a, buf, 64)) return buf;
  return L"?";
}

std::wstring format_addr6(const uint8_t* b) {
  wchar_t buf[64]{};
  in6_addr a{};
  memcpy(&a, b, 16);
  if (InetNtopW(AF_INET6, &a, buf, 64)) return buf;
  return L"?";
}

const wchar_t* tcp_state(uint32_t st) {
  switch (st) {
    case MIB_TCP_STATE_CLOSED: return L"Closed";
    case MIB_TCP_STATE_LISTEN: return L"Listen";
    case MIB_TCP_STATE_SYN_SENT: return L"SynSent";
    case MIB_TCP_STATE_SYN_RCVD: return L"SynRcvd";
    case MIB_TCP_STATE_ESTAB: return L"Established";
    case MIB_TCP_STATE_FIN_WAIT1: return L"FinWait1";
    case MIB_TCP_STATE_FIN_WAIT2: return L"FinWait2";
    case MIB_TCP_STATE_CLOSE_WAIT: return L"CloseWait";
    case MIB_TCP_STATE_CLOSING: return L"Closing";
    case MIB_TCP_STATE_LAST_ACK: return L"LastAck";
    case MIB_TCP_STATE_TIME_WAIT: return L"TimeWait";
    case MIB_TCP_STATE_DELETE_TCB: return L"DeleteTcb";
    default: return L"?";
  }
}

}  // namespace

ConnectionCollector::~ConnectionCollector() { stop(); }

void ConnectionCollector::start(uint32_t interval_ms) {
  if (running_.exchange(true)) return;
  interval_ms_ = interval_ms ? interval_ms : 2000;
  thread_ = std::thread([this] { thread_main(); });
}

void ConnectionCollector::stop() {
  if (!running_.exchange(false)) return;
  if (thread_.joinable()) thread_.join();
}

std::shared_ptr<const ConnectionSnapshot> ConnectionCollector::latest() const {
  std::lock_guard lock(mutex_);
  return latest_;
}

void ConnectionCollector::thread_main() {
  core::log::info("connection collector started");
  while (running_) {
    auto snap = std::make_shared<ConnectionSnapshot>();
    collect(*snap);
    {
      std::lock_guard lock(mutex_);
      snap->seq = latest_ ? latest_->seq + 1 : 1;
      latest_ = std::move(snap);
    }
    for (uint32_t s = 0; s < interval_ms_ && running_; s += 50) Sleep(50);
  }
  core::log::info("connection collector stopped");
}

void ConnectionCollector::collect(ConnectionSnapshot& out) {
  // --- TCP IPv4 ---
  ULONG size = 0;
  GetExtendedTcpTable(nullptr, &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0);
  std::vector<uint8_t> buf(size + 4096);
  if (GetExtendedTcpTable(buf.data(), &size, FALSE, AF_INET,
                          TCP_TABLE_OWNER_PID_ALL, 0) == NO_ERROR) {
    auto* t = reinterpret_cast<MIB_TCPTABLE_OWNER_PID*>(buf.data());
    for (DWORD i = 0; i < t->dwNumEntries; ++i) {
      const auto& e = t->table[i];
      ConnectionRow r;
      r.local_addr = format_addr(e.dwLocalAddr);
      r.local_port = ntohs(static_cast<u_short>(e.dwLocalPort));
      r.remote_addr = format_addr(e.dwRemoteAddr);
      r.remote_port = ntohs(static_cast<u_short>(e.dwRemotePort));
      r.state = e.dwState;
      r.pid = e.dwOwningPid;
      out.tcp_count++;
      if (e.dwState == MIB_TCP_STATE_ESTAB) out.established++;
      if (e.dwState == MIB_TCP_STATE_LISTEN) out.listening++;
      out.rows.push_back(std::move(r));
    }
  }

  // --- TCP IPv6 ---
  size = 0;
  GetExtendedTcpTable(nullptr, &size, FALSE, AF_INET6, TCP_TABLE_OWNER_PID_ALL, 0);
  buf.assign(size + 4096, 0);
  if (GetExtendedTcpTable(buf.data(), &size, FALSE, AF_INET6,
                          TCP_TABLE_OWNER_PID_ALL, 0) == NO_ERROR) {
    auto* t = reinterpret_cast<MIB_TCP6TABLE_OWNER_PID*>(buf.data());
    for (DWORD i = 0; i < t->dwNumEntries; ++i) {
      const auto& e = t->table[i];
      ConnectionRow r;
      r.local_addr = format_addr6(e.ucLocalAddr);
      r.local_port = ntohs(static_cast<u_short>(e.dwLocalPort));
      r.remote_addr = format_addr6(e.ucRemoteAddr);
      r.remote_port = ntohs(static_cast<u_short>(e.dwRemotePort));
      r.state = e.dwState;
      r.pid = e.dwOwningPid;
      out.tcp_count++;
      if (e.dwState == MIB_TCP_STATE_ESTAB) out.established++;
      if (e.dwState == MIB_TCP_STATE_LISTEN) out.listening++;
      out.rows.push_back(std::move(r));
    }
  }

  // --- UDP IPv4/IPv6 (no state) ---
  size = 0;
  GetExtendedUdpTable(nullptr, &size, FALSE, AF_INET, UDP_TABLE_OWNER_PID, 0);
  buf.assign(size + 4096, 0);
  if (GetExtendedUdpTable(buf.data(), &size, FALSE, AF_INET,
                          UDP_TABLE_OWNER_PID, 0) == NO_ERROR) {
    auto* t = reinterpret_cast<MIB_UDPTABLE_OWNER_PID*>(buf.data());
    for (DWORD i = 0; i < t->dwNumEntries; ++i) {
      const auto& e = t->table[i];
      ConnectionRow r;
      r.udp = true;
      r.local_addr = format_addr(e.dwLocalAddr);
      r.local_port = ntohs(static_cast<u_short>(e.dwLocalPort));
      r.pid = e.dwOwningPid;
      out.udp_count++;
      out.rows.push_back(std::move(r));
    }
  }
  size = 0;
  GetExtendedUdpTable(nullptr, &size, FALSE, AF_INET6, UDP_TABLE_OWNER_PID, 0);
  buf.assign(size + 4096, 0);
  if (GetExtendedUdpTable(buf.data(), &size, FALSE, AF_INET6,
                          UDP_TABLE_OWNER_PID, 0) == NO_ERROR) {
    auto* t = reinterpret_cast<MIB_UDP6TABLE_OWNER_PID*>(buf.data());
    for (DWORD i = 0; i < t->dwNumEntries; ++i) {
      const auto& e = t->table[i];
      ConnectionRow r;
      r.udp = true;
      r.local_addr = format_addr6(e.ucLocalAddr);
      r.local_port = ntohs(static_cast<u_short>(e.dwLocalPort));
      r.pid = e.dwOwningPid;
      out.udp_count++;
      out.rows.push_back(std::move(r));
    }
  }

  // --- Resolve process names (cached) ---
  // Rebuild name map if we see unknown pids.
  bool need_names = false;
  for (const auto& r : out.rows)
    if (r.pid && !name_cache_.count(r.pid)) { need_names = true; break; }
  if (need_names) {
    if (HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        snap != INVALID_HANDLE_VALUE) {
      PROCESSENTRY32W pe{};
      pe.dwSize = sizeof(pe);
      if (Process32FirstW(snap, &pe)) {
        do { name_cache_[pe.th32ProcessID] = pe.szExeFile; }
        while (Process32NextW(snap, &pe));
      }
      CloseHandle(snap);
    }
  }
  for (auto& r : out.rows) {
    const auto it = name_cache_.find(r.pid);
    if (it != name_cache_.end()) r.process_name = it->second;
  }
}

}  // namespace collectors
