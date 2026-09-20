#pragma once
#include <OvCore/Scripting/CSharp/FontBridge.h>

static void FontBridgeChecks(RuntimeHost& host, Scene& scene)
{
    using namespace OvReflection::Script;
    auto context = Acquire(host.NativeContext().Id());
    const uint64_t session = 0xf000000000000000ull | NextIdentity();
    auto& actor = scene.CreateActor("FontBridgeProbe"); auto& renderer = actor.AddComponent<CCanvasRenderer>();
    const uint64_t owner = OvCore::Scripting::PublishComponent(host.NativeContext(), actor.transform);
    const uint64_t rendererHandle = OvCore::Scripting::PublishComponent(host.NativeContext(), renderer);
    auto put = [](auto& bytes, size_t offset, uint32_t value) { for (size_t i = 0; i < 4; ++i) bytes[offset + i] = static_cast<uint8_t>(value >> (8 * i)); };
    auto packet = [&](size_t size = 64) {
        std::vector<uint8_t> result(size); put(result, 0, 0x3146544f); put(result, 4, 1); put(result, 8, 64); put(result, 12, static_cast<uint32_t>(size)); return result;
    };
    auto tokenPacket = [&](uint64_t token) { auto bytes = packet(); put(bytes, 24, uint32_t(token)); put(bytes, 28, uint32_t(token >> 32)); return bytes; };
    auto request = [&] {
        const std::string path = "outline-fixture.ttf";
        auto bytes = packet(48 + 3 * 80); put(bytes, 8, static_cast<uint32_t>(64 + path.size() + 12));
        put(bytes, 16, 3); put(bytes, 32, static_cast<uint32_t>(path.size())); put(bytes, 36, 64); put(bytes, 40, 8);
        std::copy(path.begin(), path.end(), bytes.begin() + 64);
        put(bytes, 64 + path.size(), 'A'); put(bytes, 68 + path.size(), 'A'); put(bytes, 72 + path.size(), 0x1f600); return bytes;
    };
    auto reject = [&](uint64_t who, uint64_t target, uint32_t operation, std::vector<uint8_t> bytes) {
        const auto original = bytes; bool rejected = false; uint64_t result = 0;
        try { QueryFont(*context, who, target, operation, bytes, result); } catch (...) { rejected = true; }
        Require(rejected && bytes == original && result == 0, "font bridge rejects malformed/stale request without output mutation");
    };
    auto malformed = request(); put(malformed, 12, 1); reject(session, owner, 100, malformed);
    malformed = request(); put(malformed, 16, UINT32_MAX); reject(session, owner, 100, malformed);
    malformed = request(); put(malformed, 52, 1); reject(session, owner, 100, malformed);
    malformed = request(); put(malformed, 64 + std::string("outline-fixture.ttf").size(), 0xd800); reject(session, owner, 100, malformed);
    auto bytes = request(); uint64_t receipt = 0;
    QueryFont(*context, session, owner, 100, bytes, receipt);
    Require(receipt != 0 && bytes[12] == 2, "production binary font bridge returns deduplicated bulk records");
    auto attach = tokenPacket(receipt); put(attach, 44, 0); QueryFont(*context, session, rendererHandle, 103, attach, receipt);
    Require(renderer.GetMaterialSlots()[0].fontPage != nullptr, "production bridge binds immutable atlas lease");
    // Recover receipt from the untouched response before output was reset by attach.
    receipt = 0; for (int i = 0; i < 8; ++i) receipt |= uint64_t(bytes[16 + i]) << (8 * i);
    reject(session + 1, owner, 102, tokenPacket(receipt));
    reject(session, rendererHandle, 101, tokenPacket(receipt));
    auto& other = scene.CreateActor("OtherFontOwner"); auto& otherRenderer = other.AddComponent<CCanvasRenderer>();
    const auto otherHandle = OvCore::Scripting::PublishComponent(host.NativeContext(), otherRenderer);
    reject(session, otherHandle, 103, tokenPacket(receipt));
    bool workerRejected = false;
    std::thread worker([&] { auto value = request(); uint64_t out = 0; try { QueryFont(*context, session, owner, 100, value, out); } catch (const Failure& e) { workerRejected = e.status == OV_SCRIPT_WRONG_THREAD; } }); worker.join();
    Require(workerRejected, "native font bridge rejects Render/worker before FreeType access");
    auto pinned = renderer.GetMaterialSlots()[0].fontPage;
    scene.DestroyActor(actor);
    reject(session, owner, 101, tokenPacket(receipt));
    auto release = tokenPacket(receipt); uint64_t released = 1; QueryFont(*context, session, owner, 102, release, released);
    Require(released == 0 && pinned->pixels.size() > 0, "expired owner receipt revocation leaves independent page lease valid");
    reject(session, owner, 102, tokenPacket(receipt));
    auto close = packet(); QueryFont(*context, session, 0, 102, close, released);
    QueryFont(*context, session, 0, 102, close, released);
    Require(pinned->pixels.size() > 0, "font session cleanup is idempotent and respects in-flight leases");
    scene.DestroyActor(other);
}
