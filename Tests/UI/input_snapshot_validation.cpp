#include <OvWindowing/Inputs/UIInputEncoding.h>
#include <OvCore/UI/Input/Physics2DQuery.h>
#include <cassert>
#include <fstream>
#include <iostream>
#include <limits>
#include <thread>

using namespace OvWindowing::Inputs;
int main(int argc, char** argv)
{
    UIInputCollector input;
    UIInputViewport viewport{{100,50},{400,300},{2,1.5}};
    input.Push({UIInputKind::PointerDown,0,{110,70}});
    input.Push({UIInputKind::PointerUp,0,{110,70}});
    input.Push({UIInputKind::KeyDown,65}); input.Push({UIInputKind::KeyRepeat,65});
    input.Push({UIInputKind::Character,0x1f642});
    input.Push({UIInputKind::Composition,0,{},UITouchPhase::Began,U"中🙂",2});
    input.Push({UIInputKind::Touch,7,{110,70},UITouchPhase::Began});
    input.Push({UIInputKind::Touch,11,{90,400},UITouchPhase::Began});
    const auto first = input.Capture(viewport,1,0.016);
    assert(first->events.size() == 8 && first->heldButtons.empty() && first->heldKeys == std::vector<int32_t>{65});
    assert(first->mousePosition.x == 20 && first->mousePosition.y == 420);
    assert(first->events.back().position.x == -20 && first->events.back().position.y == -75);
    assert(first->compositionCursor == 2 && first->composition == U"中🙂");
    const auto words = EncodeUIInput(*first);
    if (argc > 1) { std::ofstream stream(argv[1],std::ios::binary); stream.write(reinterpret_cast<const char*>(words.data()),words.size()*8); }
    input.Push({UIInputKind::Focus,0}); input.Push({UIInputKind::Character,'x'});
    const auto second = input.Capture(viewport,2,1);
    assert(!second->focused && second->events.size() == 1 && second->heldKeys.empty() && second->composition.empty());
    assert(first->events.size() == 8 && first->focused); // Published snapshot remains immutable.
    bool rejected = false; try { input.Push({UIInputKind::Character,0xd800}); } catch (const std::invalid_argument&) { rejected = true; } assert(rejected);
    rejected = false; std::thread worker([&] { try { input.Capture(viewport,3,1); } catch (const std::logic_error&) { rejected = true; } }); worker.join(); assert(rejected);
    rejected = false; try { viewport.Convert({std::numeric_limits<double>::quiet_NaN(),0}); } catch (const std::invalid_argument&) { rejected = true; } assert(rejected);
    using namespace OvCore::UI::Input;
    QueryShape2D box{QueryShape2D::Kind::Box,{0,0,0},2,1,0};
    assert(Raycast2D({{0,0,-5},{0,0,2},10},box) == 5);
    assert(!Raycast2D({{3,0,-5},{0,0,1},10},box));
    assert(Raycast2D({{-5,0,0},{1,0,0},10},box) == 3);
    box.radians = 1.5707963267948966;
    assert(!Raycast2D({{1.5,0,-5},{0,0,1},10},box));
    assert(Raycast2D({{0,1.5,-5},{0,0,1},10},box) == 5);
    QueryShape2D circle{QueryShape2D::Kind::Circle,{0,0,0},1,1,0};
    assert(Raycast2D({{-5,0,0},{1,0,0},10},circle) == 4);
    assert(!Raycast2D({{0,2,-5},{0,0,1},10},circle));
    assert(!Raycast2D({{0,0,-5},{0,0,1},4},circle));
    rejected = false; try { Raycast2D({{},{},1},circle); } catch (const std::invalid_argument&) { rejected = true; } assert(rejected);
    std::cout << "PASS input edges, viewport/DPI, Unicode, composition, multitouch IDs, focus, immutable publication, Main ownership, 2D box/circle queries\n";
}
