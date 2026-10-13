// Porte de src/cam/inlay.py.
#include "cam/inlay.hpp"

#include "cam/pocket.hpp"
#include "cam/profile.hpp"

namespace cnc::cam {

std::vector<Toolpath> generate_inlay(const std::vector<cad::EntityPtr>& entities, const Tool& tool,
                                     const cad::MaterialSetup& material, const InlaySettings& s) {
    PocketSettings pocket_settings;
    pocket_settings.name = s.name + " -- bolso";
    pocket_settings.start_depth = s.start_depth;
    pocket_settings.cut_depth = s.pocket_depth;
    std::vector<Toolpath> pocket_tps = generate_pocket(entities, tool, material, pocket_settings);
    for (Toolpath& tp : pocket_tps) tp.kind = ToolpathType::Inlay;

    ProfileSettings plug_settings;
    plug_settings.name = s.name + " -- plug";
    plug_settings.start_depth = s.start_depth;
    plug_settings.cut_depth = s.plug_depth;
    plug_settings.side = ProfileSide::Outside;
    plug_settings.direction = CutDirection::Conventional;
    plug_settings.allowance = -s.glue_allowance;
    plug_settings.separate_interior = false;
    std::vector<Toolpath> plug_tps = generate_profile(entities, tool, material, plug_settings);
    for (Toolpath& tp : plug_tps) tp.kind = ToolpathType::Inlay;

    pocket_tps.insert(pocket_tps.end(), std::make_move_iterator(plug_tps.begin()),
                      std::make_move_iterator(plug_tps.end()));
    return pocket_tps;
}

}  // namespace cnc::cam
