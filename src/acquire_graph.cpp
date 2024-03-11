#include "acquire_graph.h"

#include <algorithm>
#include <cstddef>
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "calendar.h"
#include "cata_imgui.h"
#include "color.h"
#include "imgui/imgui.h"
#include "input_context.h"
#include "item.h"
#include "item_factory.h"
#include "itype.h"
#include "localized_comparator.h"
#include "output.h"
#include "recipe.h"
#include "recipe_dictionary.h"
#include "sdltiles.h"
#include "translation.h"
#include "translations.h"
#include "type_id.h"
#include "ui_manager.h"


class my_demo_ui : public cataimgui::window
{
    public:
        my_demo_ui();
        void run();

    protected:
        void draw_controls() override;
        cataimgui::bounds get_bounds() override;
    private:
        /**
         * Data for item storage
         */
        std::vector<std::tuple<std::string, const itype *, const itype_variant_data *>> data_items;
        std::map<const itype_id, int> crafting_result_count;
        std::map<const itype_id, int> crafting_byproduct_count;
};

inline int find_default( const std::map<const itype_id, int> &m, const itype_id &key,
                         int default_value = 0 )
{
    const auto &it = m.find( key );
    if( it != m.end() ) {
        return it->second;
    }
    return default_value;
}

my_demo_ui::my_demo_ui() : cataimgui::window( _( "ImGui Demo Screen" ) )
{
    // ITEM SOURCE COUNTS
    std::vector<std::tuple<std::string, const itype *, const itype_variant_data *>> opts;
    for( const itype *i : item_controller->all() ) {
        item option( i, calendar::turn_zero );

        if( i->variant_kind == itype_variant_kind::gun || i->variant_kind == itype_variant_kind::generic ) {
            for( const itype_variant_data &variant : i->variants ) {
                const std::string gun_variant_name = variant.alt_name.translated();
                const itype_variant_data *ivd = &variant;
                opts.emplace_back( gun_variant_name, i, ivd );
            }
        }
        option.clear_itype_variant();
        opts.emplace_back( option.tname( 1, false ), i, nullptr );
    }
    std::sort( opts.begin(), opts.end(), localized_compare );
    data_items = opts;

    for( auto it = recipe_dict.begin(); it != recipe_dict.end(); ++it ) {
        const itype_id r_id = it->second.result();
        if( !crafting_result_count.count( r_id ) ) {
            crafting_result_count[r_id] = 0;
        }
        crafting_result_count[r_id] += 1;

        for( /*std::map<itype_id, int>*/ auto const& [key, _] : it->second.get_byproducts() ) {
            if( !crafting_byproduct_count.count( key ) ) {
                crafting_byproduct_count[key] = 0;
            }
            crafting_byproduct_count[key] += 1;
        }
    }
}

cataimgui::bounds my_demo_ui::get_bounds()
{
    return { -1.f, -1.f, float( str_width_to_pixels( TERMX ) ), float( str_height_to_pixels( TERMY ) ) };
}

void my_demo_ui::draw_controls()
{
    if( ! ImGui::BeginTable( "ACQUIRE_GRAPH", 4,
                             ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable | ImGuiTableColumnFlags_WidthFixed ) ) {
        return;
    }
    ImGui::TableSetupColumn( "?", 0, ImGui::CalcTextSize( "0" ).x );
    ImGui::TableSetupColumn( "itemo namae" );
    ImGui::TableSetupColumn( "itemo resuloto counto" );
    ImGui::TableSetupColumn( "itemo byproducoto counto" );

    for( size_t i = 0; i < data_items.size(); i++ ) {
        item ity( std::get<1>( data_items[i] ), calendar::turn_zero );

        ImGui::TableNextColumn();
        draw_colored_text( ity.symbol(), ity.color() );

        std::string i_name = std::get<0>( data_items[i] );
        if( std::get<2>( data_items[i] ) != nullptr ) {
            i_name += "<color_dark_gray>(V)</color>";
        }
        if( !std::get<1>( data_items[i] )->snippet_category.empty() ) {
            i_name += "<color_yellow>(S)</color>";
        }

        ImGui::TableNextColumn();
        draw_colored_text( i_name.c_str(), c_white );

        const itype_id &iid = std::get<1>( data_items[i] )->get_id();

        ImGui::TableNextColumn();
        int count = find_default( crafting_result_count, iid, 0 );
        ImGui::Text( "%d", count );

        ImGui::TableNextColumn();
        count = find_default( crafting_byproduct_count, iid, 0 );
        ImGui::Text( "%d", count );
    }

    ImGui::EndTable();
}

void my_demo_ui::run()
{
    input_context ctxt( "HELP_KEYBINDINGS" );
    ctxt.register_action( "QUIT" );
    ctxt.register_action( "SELECT" );
    ctxt.register_action( "MOUSE_MOVE" );
    ctxt.register_action( "ANY_INPUT" );
    ctxt.register_action( "HELP_KEYBINDINGS" );
    std::string action;

    ui_manager::redraw();

    while( is_open ) {
        ui_manager::redraw();
        action = ctxt.handle_input();
        if( action == "QUIT" ) {
            break;
        }
    }
}


void acquire_graph()
{
    my_demo_ui dui;
    dui.run();
}
