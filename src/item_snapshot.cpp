#include "item_snapshot.h"

#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <tuple>
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
#include "translation.h"
#include "translations.h"
#include "type_id.h"
#include "ui_manager.h"


class item_snapshot_impl
{
        friend class item_snapshot_ui;
    public:
        item_snapshot_impl();
        void add_want( itype_id id, int count );
    private:
        int selected_id = -1;
        std::vector<std::tuple<std::string, const itype *, const itype_variant_data *>> data_items;
        std::map<const itype_id, int> item_want;
};

class item_snapshot_ui : public cataimgui::window
{
    public:
        item_snapshot_ui( item_snapshot_impl *pimpl_in );
        void run();
    protected:
        void draw_controls() override;
        cataimgui::bounds get_bounds() override;
    private:
        void show_want_table();
        item_snapshot_impl *pimpl;
};

item_snapshot_impl::item_snapshot_impl()
{
    // data_items
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
}

void item_snapshot_impl::add_want( itype_id id, int count )
{
    if( !item_want.count( id ) ) {
        item_want[id] = 0;
    }
    item_want[id] += count;
}

item_snapshot_ui::item_snapshot_ui( item_snapshot_impl *pimpl_in )
    : cataimgui::window( _( "Item snapshot" ) ), pimpl( pimpl_in )
{
}

cataimgui::bounds item_snapshot_ui::get_bounds()
{
    return { -1.f, -1.f, float( str_width_to_pixels( TERMX ) ), float( str_height_to_pixels( TERMY ) ) };
}

void item_snapshot_ui::show_want_table()
{
    if( ! ImGui::BeginTable( "ITEM_SNAPSHOT_WANT", 3,
                             ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersOuter ) ) {
        return;
    }

    ImGui::TableSetupColumn( "?", 0, ImGui::CalcTextSize( "0" ).x );
    ImGui::TableSetupColumn( "itemo namae" );
    ImGui::TableSetupColumn( "count" );
    ImGui::TableHeadersRow();

    for( auto const& /*const itype_id, int*/[itm_id, count] : pimpl->item_want ) {
        item ity( itm_id, calendar::turn_zero, count );

        ImGui::TableNextColumn();
        draw_colored_text( ity.symbol(), ity.color() );

        ImGui::TableNextColumn();
        std::string i_name = ity.tname();
        draw_colored_text( i_name.c_str(), c_white );

        ImGui::TableNextColumn();
        draw_colored_text( std::to_string( count ), c_white );
    }

    ImGui::EndTable();
}

void item_snapshot_ui::draw_controls()
{
    ImGui::Text( "selected_id: %d", pimpl->selected_id );
    if( pimpl->selected_id != -1 && ImGui::Button( "Add Item to Want" ) ) {
        pimpl->add_want( ( std::get<1>( pimpl->data_items[pimpl->selected_id] ) )->get_id(), 1 );
    }

    const float TEXT_BASE_HEIGHT = ImGui::GetTextLineHeightWithSpacing();
    ImVec2 outer_size = ImVec2( 0.0f, TEXT_BASE_HEIGHT * 20 );
    if( ! ImGui::BeginTable( "ITEM_SNAPSHOT", 2,
                             ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable
                             | ImGuiTableFlags_BordersOuter,
                             outer_size ) ) {
        return;
    }
    ImGui::TableSetupColumn( "?", 0, ImGui::CalcTextSize( "0" ).x );
    ImGui::TableSetupColumn( "itemo namae" );
    ImGui::TableHeadersRow();

    ImGuiListClipper clipper;
    clipper.Begin( pimpl->data_items.size() );
    while( clipper.Step() ) {
        for( int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++ ) {
            item ity( std::get<1>( pimpl->data_items[i] ), calendar::turn_zero );

            ImGui::TableNextColumn();
            if( ImGui::Selectable( ( "##" + std::to_string( i ) ).c_str(), pimpl->selected_id == i,
                                   ImGuiSelectableFlags_SpanAllColumns )
              ) {
                pimpl->selected_id = i;
            }
            ImGui::SameLine();

            draw_colored_text( ity.symbol(), ity.color() );

            std::string i_name = std::get<0>( pimpl->data_items[i] );
            if( std::get<2>( pimpl->data_items[i] ) != nullptr ) {
                i_name += "<color_dark_gray>(V)</color>";
            }
            if( !std::get<1>( pimpl->data_items[i] )->snippet_category.empty() ) {
                i_name += "<color_yellow>(S)</color>";
            }

            ImGui::TableNextColumn();
            draw_colored_text( i_name.c_str(), c_white );
        }
    }
    ImGui::EndTable();

    // For not jagging up when table leaves the screen (msg too long)
    ImGui::BeginChild( "Descriptions" );
    show_want_table();
    ImGui::EndChild();
}

void item_snapshot_ui::run()
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
        action = ctxt.handle_input( 17 );
        if( action == "QUIT" ) {
            break;
        }
    }
}

// These need to be here so that pimpl works with unique ptr
item_snapshot::item_snapshot() = default;
item_snapshot::~item_snapshot() = default;

bool item_snapshot::store()
{
    return true;
}

void item_snapshot::load()
{
}

void item_snapshot::show()
{
    if( pimpl == nullptr ) {
        pimpl = std::make_unique<item_snapshot_impl>();
    }
    item_snapshot_ui itm_sn_ui( pimpl.get() );
    itm_sn_ui.run();
}
