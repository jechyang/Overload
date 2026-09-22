from pathlib import Path

# Generates Examples/UIGallery/Assets/Scenes/UIGallery.ovscene
# Actor table: id, name, parent, guid, behaviours, rt, extra_components

def transform():
    return """                    <component>
                        <type>class OvCore::ECS::Components::CTransform</type>
                        <data>
                            <position><x>0</x><y>0</y><z>0</z></position>
                            <rotation><x>0</x><y>0</y><z>0</z><w>1</w></rotation>
                            <scale><x>1</x><y>1</y><z>1</z></scale>
                        </data>
                    </component>"""

def camera():
    return """                    <component>
                        <type>class OvCore::ECS::Components::CCamera</type>
                        <data>
                            <fov>60</fov>
                            <size>5</size>
                            <near>0.1</near>
                            <far>1000</far>
                            <clear_color><x>0.05</x><y>0.06</y><z>0.08</z></clear_color>
                            <frustum_geometry_culling>true</frustum_geometry_culling>
                            <frustum_light_culling>true</frustum_light_culling>
                            <projection_mode>1</projection_mode>
                        </data>
                    </component>"""

def canvas_comp():
    return """                    <component>
                        <type>class OvCore::ECS::Components::CCanvas</type>
                        <data>
                            <scale_factor>1</scale_factor>
                            <sorting_order>0</sorting_order>
                            <override_sorting>false</override_sorting>
                        </data>
                    </component>"""

def rt_stretch():
    return """                    <component>
                        <type>class OvCore::ECS::Components::CRectTransform</type>
                        <data>
                            <anchor_min><x>0</x><y>0</y></anchor_min>
                            <anchor_max><x>1</x><y>1</y></anchor_max>
                            <pivot><x>0.5</x><y>0.5</y></pivot>
                            <anchored_position><x>0</x><y>0</y></anchored_position>
                            <size_delta><x>0</x><y>0</y></size_delta>
                        </data>
                    </component>"""

def rt_center(w, h, x=0, y=0):
    return f"""                    <component>
                        <type>class OvCore::ECS::Components::CRectTransform</type>
                        <data>
                            <anchor_min><x>0.5</x><y>0.5</y></anchor_min>
                            <anchor_max><x>0.5</x><y>0.5</y></anchor_max>
                            <pivot><x>0.5</x><y>0.5</y></pivot>
                            <anchored_position><x>{x}</x><y>{y}</y></anchored_position>
                            <size_delta><x>{w}</x><y>{h}</y></size_delta>
                        </data>
                    </component>"""

def behaviour(type_name):
    return f"""                    <behaviour>
                        <type>UIGallery.{type_name}</type>
                        <script_identity backend="csharp" assembly="GameScripts" type="UIGallery.{type_name}" id=""/>
                        <data/>
                    </behaviour>"""

actors = []

def add(aid, name, parent, guid, behaviours, comps, rtag=None):
    actors.append(dict(id=aid, name=name, parent=parent, guid=guid, behaviours=behaviours, comps=comps))

# Camera
add(100, "Camera", 0, 1369332637595243126, [], [transform(), camera()])

# EventSystem
add(200, "EventSystem", 0, 5550000000000000201, ["EventSystemRoot", "InputModule"], [transform()])

# Canvas root stacks Header / Body / Footer.
add(1, "Canvas", 0, 709581266370247569, ["GalleryCanvas", "RootStack"], [transform(), rt_stretch(), canvas_comp()])

# Header
add(2, "Header", 1, 5550000000000000002, ["HeaderLayout", "PanelImage", "HeaderBar"], [transform(), rt_stretch()])
add(3, "Title", 2, 5550000000000000003, ["TitleText", "TitleOutline"], [transform(), rt_center(400, 36)])

# Body row: three columns
add(10, "Body", 1, 5550000000000000010, ["RowLayout", "BodyFlexible"], [transform(), rt_stretch()])

# Left column — layout controllers
add(11, "LayoutColumn", 10, 5550000000000000011, ["ColumnLayout", "PanelImage", "PanelBox"], [transform(), rt_center(280, 400)])
add(12, "LayoutTitle", 11, 5550000000000000012, ["SectionHeader", "FixedLabel"], [transform(), rt_center(200, 20)])
add(13, "CardA", 11, 5550000000000000013, ["SoftImage", "CardSize"], [transform(), rt_center(72, 28)])
add(14, "CardB", 11, 5550000000000000014, ["BlueTile", "CardSize"], [transform(), rt_center(72, 28)])
add(15, "NestedColumn", 11, 5550000000000000015, ["ColumnLayout", "SlateTile", "PanelBox"], [transform(), rt_center(120, 80)])
add(16, "CardC", 15, 5550000000000000016, ["OrangeTile", "CardSize"], [transform(), rt_center(72, 28)])
add(17, "CardD", 15, 5550000000000000017, ["TealTile", "CardSize"], [transform(), rt_center(72, 28)])
add(18, "AspectNote", 11, 5550000000000000018, ["Caption"], [transform(), rt_center(200, 36)])

# Middle column — grid + aspect + checker
add(20, "GridColumn", 10, 5550000000000000020, ["ColumnLayout", "PanelImage", "PanelBox"], [transform(), rt_center(280, 400)])
add(21, "GridTitle", 20, 5550000000000000021, ["SectionHeader", "FixedLabel"], [transform(), rt_center(200, 20)])
add(22, "GridPanel", 20, 5550000000000000022, ["DemoGrid", "SoftImage", "GridBox"], [transform(), rt_center(180, 140)])
tile_kinds = ["BlueTile", "TealTile", "OrangeTile", "PurpleTile", "SlateTile", "CoralTile", "TealTile", "OrangeTile", "BlueTile"]
for i, kind in enumerate(tile_kinds):
    add(30 + i, f"Cell{i+1}", 22, 5550000000000000030 + i, [kind, "TileSize"], [transform(), rt_center(48, 36)])
add(40, "AspectFrame", 20, 5550000000000000040, ["SlateTile", "TallCard"], [transform(), rt_center(160, 100)])
add(41, "AspectSquare", 40, 5550000000000000041, ["SquareAspect", "CheckerGraphic"], [transform(), rt_center(90, 90)])
add(42, "AspectCaption", 20, 5550000000000000042, ["Caption"], [transform(), rt_center(200, 28)])

# Right column — clip / mask / canvas group
add(50, "ClipColumn", 10, 5550000000000000050, ["ColumnLayout", "PanelImage", "PanelBox"], [transform(), rt_center(280, 400)])
add(51, "ClipTitle", 50, 5550000000000000051, ["SectionHeader", "FixedLabel"], [transform(), rt_center(200, 20)])
add(52, "ClipPanel", 50, 5550000000000000052, ["ClipWindow", "SoftImage", "ClipBox"], [transform(), rt_center(220, 70)])
add(53, "ClipCopy", 52, 5550000000000000053, ["ClipCopy"], [transform(), rt_center(200, 120)])
add(54, "MaskPanel", 50, 5550000000000000054, ["StencilWindow", "SoftImage", "ClipBox"], [transform(), rt_center(220, 70)])
add(55, "MaskA", 54, 5550000000000000055, ["CoralTile", "TileSize"], [transform(), rt_center(48, 36, -40, 0)])
add(56, "MaskB", 54, 5550000000000000056, ["OrangeTile", "TileSize"], [transform(), rt_center(48, 36, 10, 8)])
add(57, "MaskC", 54, 5550000000000000057, ["PurpleTile", "TileSize"], [transform(), rt_center(48, 36, 50, -10)])
add(58, "GroupPanel", 50, 5550000000000000058, ["DimGroup", "PanelImage", "ClipBox"], [transform(), rt_center(220, 50)])
add(59, "GroupChild", 58, 5550000000000000059, ["Caption"], [transform(), rt_center(180, 24)])

# Footer — text effects + events
add(70, "Footer", 1, 5550000000000000070, ["RowLayout", "PanelImage", "FooterBar"], [transform(), rt_stretch()])
add(71, "OutlineBox", 70, 5550000000000000071, ["OutlineLabel", "SoftOutline", "FixedLabel"], [transform(), rt_center(100, 32)])
add(72, "ShadowBox", 70, 5550000000000000072, ["ShadowLabel", "DropShadow", "FixedLabel"], [transform(), rt_center(100, 32)])
add(73, "RichLine", 70, 5550000000000000073, ["RichLine", "CardSize"], [transform(), rt_center(160, 28)])
add(74, "CjkLine", 70, 5550000000000000074, ["CjkLine", "CardSize"], [transform(), rt_center(160, 28)])
add(75, "BestFitBox", 70, 5550000000000000075, ["BestFitLine", "CardSize"], [transform(), rt_center(120, 36)])
add(76, "InteractBox", 70, 5550000000000000076, ["RowLayout", "CardSize"], [transform(), rt_center(200, 40)])
add(77, "PulseTarget", 76, 5550000000000000077, ["PulseTarget", "TileSize"], [transform(), rt_center(56, 34)])
add(78, "Hint", 76, 5550000000000000078, ["Hint", "CardSize"], [transform(), rt_center(120, 28)])

parts = ["<root>", "<scene>", "<actors>"]
for a in actors:
    comps = "\n".join(a["comps"])
    behs = "\n".join(behaviour(b) for b in a["behaviours"])
    if not a["behaviours"]:
        beh_xml = "                <behaviours/>"
    else:
        beh_xml = "                <behaviours>\n" + behs + "\n                </behaviours>"
    parts.append(f"""            <actor>
                <name>{a['name']}</name>
                <tag></tag>
                <active>true</active>
                <id>{a['id']}</id>
                <guid>{a['guid']}</guid>
                <prefab_source></prefab_source>
                <prefab_node_guid>0</prefab_node_guid>
                <parent>{a['parent']}</parent>
                <components>
{comps}
                </components>
{beh_xml}
            </actor>""")
parts += ["</actors>", "</scene>", "</root>"]
out = Path(r"H:\overload\Overload\Examples\UIGallery\Assets\Scenes\UIGallery.ovscene")
out.parent.mkdir(parents=True, exist_ok=True)
out.write_text("\n".join(parts) + "\n", encoding="utf-8")
print(f"wrote {out} actors={len(actors)} bytes={out.stat().st_size}")
