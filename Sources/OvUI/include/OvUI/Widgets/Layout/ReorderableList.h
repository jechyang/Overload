#pragma once
#include <imgui.h>
#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace OvUI::Widgets::Layout
{
    // Presentation state only. All callbacks are consumed synchronously during Draw;
    // adapters own data validation, lifetime/thread guards, and scene transactions.
    class ReorderableList
    {
    public:
        static constexpr size_t NoSelection=size_t(-1), PageSize=32;
        struct Rect { ImVec2 min{},max{}; };
        struct RowLayout { size_t index; Rect row,handle; };
        struct Layout { Rect header,size,add,remove; std::vector<RowLayout> rows; };
        struct Model
        {
            std::string name;
            size_t limit=1024;
            bool resizable=true,readOnly=false,mixedShape=false,mixedValues=false,expandForSearch=false;
            std::string emptyText="List is empty";
            std::function<size_t()> count;
            std::function<bool(size_t)> resize,erase,duplicate;
            std::function<bool(size_t,size_t)> move;
            std::function<bool(size_t)> visible;
            std::function<void(size_t)> drawElement;
            std::function<void()> header,pageChanged;
            std::function<bool()> stopDrawing;
        };
        ReorderableList();
        ReorderableList(const ReorderableList&)=delete;
        ReorderableList& operator=(const ReorderableList&)=delete;
        ReorderableList(ReorderableList&&)=default;
        ReorderableList& operator=(ReorderableList&&)=default;
        size_t page=0,selected=NoSelection;
        void Invalidate() { ++m_epoch; }
        void ClearLayout() { m_layout={}; }
        const Layout& LastLayout() const { return m_layout; }
        void Draw(const Model&);
    private:
        bool Resize(const Model&,size_t);
        bool Erase(const Model&,size_t);
        bool Move(const Model&,size_t,size_t);
        bool Duplicate(const Model&,size_t);
        bool StopDrawing(const Model& model) const { return m_structureChanged || (model.stopDrawing && model.stopDrawing()); }
        void Clamp(size_t);
        void DrawRows(const Model&,size_t);
        void DrawFooter(const Model&,size_t);
        void DrawItemMenu(const Model&,size_t,size_t);
        uint64_t m_owner=0,m_epoch=0;
        Layout m_layout;
        std::array<char,32> m_sizeText{};
        size_t m_lastSize=NoSelection;
        bool m_sizeInvalid=false,m_structureChanged=false;
    };
}
