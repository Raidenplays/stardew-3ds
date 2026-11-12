#include "cpp_pad.h"

#include <3ds.h>
#include <malloc.h>
#include <string.h>

// The protocol is from 3dbrew (Circle Pad Pro, IR Services, IRUSER Shared Memory): connect to device 1,
// ask for input every N ms, and read the 6-byte reports the service leaves in shared memory.
namespace cpp_pad {

namespace {
const u32 MEM_SIZE = 0x1000;
const u32 PACKET_COUNT = 1, PACKET_INFO = 8, MAX_PACKET = 32;
const u32 BUF_SIZE = PACKET_COUNT * (PACKET_INFO + MAX_PACKET);
const u8 DEVICE_CPP = 1;
const u8 PERIOD_CONNECT = 8, PERIOD = 16;     // ms between reports

Handle srv = 0, memHandle = 0, connEvent = 0, recvEvent = 0;
u8* mem = nullptr;
Thread thread = nullptr;
volatile bool running = false;
volatile int padState = 0;
volatile int rawX = 0x800, rawY = 0x800, buttons = 0;   // buttons: bit0 ZL, bit1 ZR, bit2 R
int centreX = 0x800, centreY = 0x800;
bool haveCentre = false;
int rangeX = 1100, rangeY = 1100;
bool calibrated = false;

Result cmd0(u32 header) {
    u32* c = getThreadCommandBuffer();
    c[0] = header;
    Result r = svcSendSyncRequest(srv);
    return R_FAILED(r) ? r : (Result)c[1];
}
Result cmd1(u32 header, u32 a) {
    u32* c = getThreadCommandBuffer();
    c[0] = header;
    c[1] = a;
    Result r = svcSendSyncRequest(srv);
    return R_FAILED(r) ? r : (Result)c[1];
}
Result getEvent(u32 header, Handle* out) {
    u32* c = getThreadCommandBuffer();
    c[0] = header;
    Result r = svcSendSyncRequest(srv);
    if (R_FAILED(r)) return r;
    *out = c[3];
    return (Result)c[1];
}
Result sendPayload(const u8* data, u32 size) {
    u32* c = getThreadCommandBuffer();
    c[0] = 0x000D0042;                         // SendIrnop
    c[1] = size;
    c[2] = (size << 14) | 2;
    c[3] = (u32)data;
    Result r = svcSendSyncRequest(srv);
    return R_FAILED(r) ? r : (Result)c[1];
}
void askForInput(u8 periodMs) {
    u8 req[3] = {1, periodMs, (u8)((periodMs + 2) << 2)};
    sendPayload(req, 3);
}
int connection() { return mem ? mem[0x8] : 0; }   // StatusInfo: 0 none, 1 connecting, 2 connected

bool wait(Handle h, int ms) { return svcWaitSynchronization(h, (s64)ms * 1000000LL) == 0; }

// Reads whatever reports are waiting; true if there was one.
bool takeReports() {
    u32 start, count;
    memcpy(&start, mem + 0x10, 4);
    memcpy(&count, mem + 0x18, 4);
    if (count == 0 || count > PACKET_COUNT) { if (count) cmd1(0x00190040, count); return false; }
    bool got = false;
    const u8* table = mem + 0x20;
    const u8* data = table + PACKET_INFO * PACKET_COUNT;
    const u32 dataSize = BUF_SIZE - PACKET_INFO * PACKET_COUNT;
    for (u32 i = 0; i < count; i++) {
        u32 off, len;
        memcpy(&off, table + ((start + i) % PACKET_COUNT) * PACKET_INFO, 4);
        memcpy(&len, table + ((start + i) % PACKET_COUNT) * PACKET_INFO + 4, 4);
        if (len < 4 || len > MAX_PACKET) continue;
        u8 pkt[MAX_PACKET];
        for (u32 k = 0; k < len; k++) pkt[k] = data[(off + k) % dataSize];   // the buffer wraps
        bool large = pkt[2] & 0x40;
        u32 payloadLen = large ? ((u32)(pkt[2] & 0x3F) << 8) | pkt[3] : (u32)(pkt[2] & 0x3F);
        u32 at = large ? 4 : 3;
        if (at + payloadLen > len || payloadLen < 6 || pkt[at] != 0x10) continue;
        const u8* p = pkt + at;
        rawX = p[1] | ((p[2] & 0x0F) << 8);
        rawY = (p[2] >> 4) | (p[3] << 4);
        buttons = ((p[4] & 0x20) ? 0 : 1) | ((p[4] & 0x40) ? 0 : 2) | ((p[4] & 0x80) ? 0 : 4);   // a set bit means not pressed
        if (!haveCentre && !calibrated) { centreX = rawX; centreY = rawY; haveCentre = true; }   // wherever it rests when found
        got = true;
    }
    cmd1(0x00190040, count);                   // ReleaseReceivedData
    return got;
}

void worker(void*) {
    while (running) {
        padState = 1;
        // connecting takes persistence: ask, wait a moment, and if it didn't take, hang up and ask again
        while (running && connection() != 2) {
            cmd1(0x00060040, DEVICE_CPP);      // RequireConnection
            wait(connEvent, 100);
            if (connection() == 2) break;
            cmd0(0x00090000);                  // Disconnect
            wait(connEvent, 100);
        }
        if (!running) break;
        bool talking = false;
        for (int tries = 0; running && tries < 40 && !talking; tries++) {
            askForInput(PERIOD_CONNECT);
            if (wait(recvEvent, PERIOD_CONNECT * 16)) talking = takeReports();
        }
        if (!talking) { cmd0(0x00090000); continue; }
        padState = 2;
        askForInput(PERIOD);
        int quiet = 0;
        while (running && connection() == 2) {
            if (wait(recvEvent, 250)) { takeReports(); quiet = 0; }
            else if (++quiet >= 8) break;      // two seconds of nothing: it was taken off or turned away
            else askForInput(PERIOD);          // remind it we're listening
        }
        rawX = centreX; rawY = centreY; buttons = 0;
        cmd0(0x00090000);
        svcSleepThread(200000000LL);
    }
    padState = 0;
}
}  // namespace

void start() {
    if (running || thread) return;
    bool isNew = false;
    APT_CheckNew3DS(&isNew);
    if (isNew) return;                         // a New 3DS has the stick built in (read through ir:rst)
    if (R_FAILED(srvGetServiceHandle(&srv, "ir:USER"))) { srv = 0; return; }
    mem = (u8*)memalign(0x1000, MEM_SIZE);
    if (!mem) { svcCloseHandle(srv); srv = 0; return; }
    memset(mem, 0, MEM_SIZE);
    if (R_FAILED(svcCreateMemoryBlock(&memHandle, (u32)mem, MEM_SIZE, MEMPERM_READ, MEMPERM_READWRITE))) {
        free(mem); mem = nullptr;
        svcCloseHandle(srv); srv = 0;
        return;
    }
    u32* c = getThreadCommandBuffer();
    c[0] = 0x00180182;                         // InitializeIrnopShared
    c[1] = MEM_SIZE;
    c[2] = BUF_SIZE;                           // receive buffer
    c[3] = PACKET_COUNT;
    c[4] = BUF_SIZE;                           // send buffer
    c[5] = PACKET_COUNT;
    c[6] = 4;                                  // bit rate: 96000 bps
    c[7] = 0;
    c[8] = memHandle;
    Result r = svcSendSyncRequest(srv);
    if (R_FAILED(r) || R_FAILED((Result)c[1]) || R_FAILED(getEvent(0x000C0000, &connEvent)) || R_FAILED(getEvent(0x000A0000, &recvEvent))) {
        stop();
        return;
    }
    if (!calibrated) haveCentre = false;
    running = true;
    padState = 1;
    s32 prio = 0x30;
    svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
    thread = threadCreate(worker, nullptr, 16 * 1024, prio - 1, -2, false);
    if (!thread) { running = false; stop(); }
}

void stop() {
    running = false;
    if (thread) {
        threadJoin(thread, 2000000000ULL);
        threadFree(thread);
        thread = nullptr;
    }
    if (srv) {
        cmd0(0x00090000);                      // Disconnect
        cmd0(0x00020000);                      // FinalizeIrnop
    }
    if (connEvent) { svcCloseHandle(connEvent); connEvent = 0; }
    if (recvEvent) { svcCloseHandle(recvEvent); recvEvent = 0; }
    if (srv) { svcCloseHandle(srv); srv = 0; }
    if (memHandle) { svcCloseHandle(memHandle); memHandle = 0; }
    if (mem) { free(mem); mem = nullptr; }
    padState = 0;
    rawX = centreX; rawY = centreY; buttons = 0;
}

int state() { return padState; }

void read(float& x, float& y, bool& zl, bool& zr, bool& r) {
    x = y = 0;
    zl = zr = r = false;
    if (padState != 2) return;
    // about +-1200 either way from rest; a small dead zone in the middle
    float fx = (rawX - centreX) / (float)rangeX, fy = (rawY - centreY) / (float)rangeY;
    if (fx > -0.25f && fx < 0.25f) fx = 0;
    if (fy > -0.25f && fy < 0.25f) fy = 0;
    x = fx > 1 ? 1 : fx < -1 ? -1 : fx;
    y = fy > 1 ? -1 : fy < -1 ? 1 : -fy;       // up on the stick is up on the screen
    zl = buttons & 1;
    zr = buttons & 2;
    r = buttons & 4;
}

void raw(int& x, int& y, int& b) { x = rawX; y = rawY; b = buttons; }

void calibrate(int cx, int cy, int rx, int ry) {
    if (rx <= 0 || ry <= 0) { calibrated = false; haveCentre = false; rangeX = rangeY = 1100; return; }
    centreX = cx; centreY = cy;
    rangeX = rx < 200 ? 200 : rx;
    rangeY = ry < 200 ? 200 : ry;
    calibrated = true;
}

}  // namespace cpp_pad
