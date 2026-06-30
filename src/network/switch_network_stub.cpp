// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version

#include "network/network.h"
#include "network/artic_base/artic_base_client.h"
#include "network/room_member.h"
#include "network/socket_manager.h"

namespace Network {
namespace {

std::shared_ptr<RoomMember> g_room_member;

} // namespace

bool Init() {
    g_room_member = std::make_shared<RoomMember>();
    return true;
}

std::weak_ptr<Room> GetRoom() {
    return {};
}

std::weak_ptr<RoomMember> GetRoomMember() {
    return g_room_member;
}

void Shutdown() {
    g_room_member.reset();
}

std::atomic<u32> SocketManager::count{0};

void SocketManager::EnableSockets() {}

void SocketManager::DisableSockets() {}

class RoomMember::RoomMemberImpl {};

RoomMember::RoomMember() : room_member_impl{std::make_unique<RoomMemberImpl>()} {}

RoomMember::~RoomMember() = default;

RoomMember::State RoomMember::GetState() const {
    return State::Idle;
}

const MacAddress& RoomMember::GetMacAddress() const {
    static constexpr MacAddress empty_mac{};
    return empty_mac;
}

bool RoomMember::IsConnected() const {
    return false;
}

void RoomMember::SendWifiPacket(const WifiPacket&) {}

void RoomMember::SendGameInfo(const GameInfo&) {}

RoomMember::CallbackHandle<WifiPacket> RoomMember::BindOnWifiPacketReceived(
    std::function<void(const WifiPacket&)> callback) {
    return std::make_shared<std::function<void(const WifiPacket&)>>(std::move(callback));
}

template <typename T>
void RoomMember::Unbind(CallbackHandle<T>) {}

template void RoomMember::Unbind(CallbackHandle<WifiPacket>);

namespace ArticBase {

bool Client::Request::AddParameterS8(s8) {
    return false;
}

bool Client::Request::AddParameterS16(s16) {
    return false;
}

bool Client::Request::AddParameterS32(s32) {
    return false;
}

bool Client::Request::AddParameterS64(s64) {
    return false;
}

bool Client::Request::AddParameterBuffer(const void*, size_t) {
    return false;
}

Client::Request::Request(u32 request_id, const std::string& method, size_t max_params)
    : method_name(method), max_param_count(max_params) {
    (void)request_id;
}

void Client::UDPStream::Start() {
    ready = false;
}

void Client::UDPStream::Handle() {}

Client::~Client() = default;

bool Client::Connect() {
    connected = false;
    return false;
}

std::shared_ptr<Client::UDPStream> Client::NewUDPStream(
    const std::string, size_t buffer_size, const std::chrono::milliseconds& read_interval) {
    return std::make_shared<UDPStream>(*this, 0, buffer_size, read_interval);
}

void Client::StopImpl(bool) {
    connected = false;
}

std::optional<std::pair<void*, size_t>> Client::Response::GetResponseBuffer(u32) const {
    return std::nullopt;
}

std::optional<Client::Response> Client::Send(Request&) {
    return std::nullopt;
}

void Client::LogOnServer(ArticBaseCommon::LogOnServerType, const std::string&) {}

void Client::SignalCommunicationError(const std::string& msg) {
    if (communication_error_callback) {
        communication_error_callback(msg);
    }
}

void Client::PingFunction() {}

bool Client::ConnectWithTimeout(SocketHolder, void*, size_t, int) {
    return false;
}

bool Client::SetNonBlock(SocketHolder, bool) {
    return false;
}

bool Client::Read(SocketHolder, void*, size_t, const std::chrono::nanoseconds&) {
    return false;
}

bool Client::Write(SocketHolder, const void*, size_t, const std::chrono::nanoseconds&) {
    return false;
}

std::optional<ArticBaseCommon::DataPacket> Client::SendRequestPacket(
    const ArticBaseCommon::RequestPacket&, bool,
    const std::vector<ArticBaseCommon::RequestParameter>&, const std::chrono::nanoseconds&) {
    return std::nullopt;
}

std::optional<std::string> Client::SendSimpleRequest(const std::string&) {
    return std::nullopt;
}

Client::Handler::Handler(Client& client, u32 addr, u16 port, int id_)
    : id(id_), client(client), addr(addr), port(port) {}

void Client::Handler::RunLoop() {}

void Client::OnAllHandlersFinished() {}

} // namespace ArticBase
} // namespace Network
