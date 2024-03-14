#include "acquire_graph.h"

#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "avatar.h"
#include "calendar.h"
#include "cata_imgui.h"
#include "cata_path.h"
#include "cata_utility.h"
#include "catacharset.h"
#include "color.h"
#include "filesystem.h"
#include "flexbuffer_json-inl.h"
#include "flexbuffer_json.h"
#include "imgui/imgui.h"
#include "input_context.h"
#include "item.h"
#include "item_factory.h"
#include "itype.h"
#include "json.h"
#include "json_error.h"
#include "localized_comparator.h"
#include "output.h"
#include "path_info.h"
#include "recipe.h"
#include "recipe_dictionary.h"
#include "requirements.h"
#include "string_formatter.h"
#include "translation.h"
#include "translations.h"
#include "type_id.h"
#include "ui_manager.h"


struct AbstractN {
    virtual std::string name() = 0;
};

/**
 * Item a person wants to obtain.
 */
struct ItemN : AbstractN {
    std::string name() override;
    item itm;
};

enum class crafting_source : int {
    product,
    byproduct,
};

struct recipe_source {
    recipe_source( const recipe_id &rin, const crafting_source &sin )
        : r( rin ), s( sin ) {};
    const recipe_id &r;
    const crafting_source &s;
};

class acquire_graph_impl
{
        friend class acquire_graph;
        friend class acquire_graph_ui;
    public:
        acquire_graph_impl();
        void add_head( std::shared_ptr<AbstractN> );
        void add_item( const item &it );

        std::vector<std::shared_ptr<AbstractN>>::iterator heads_begin();
        std::vector<std::shared_ptr<AbstractN>>::iterator heads_end();
    private:
        int selected_id = -1;
        std::vector<std::shared_ptr<AbstractN>> graph_heads;
        /**
         * Data for item storage.
         */
        std::vector<std::tuple<std::string, const itype *, const itype_variant_data *>> data_items;
        std::map<const itype_id, int> crafting_result_count;
        std::map<const itype_id, int> crafting_byproduct_count;
        std::map<const itype_id, std::vector<std::pair<recipe_id, crafting_source>>> from_crafting;
};

class acquire_graph_ui : public cataimgui::window
{
    public:
        acquire_graph_ui( acquire_graph_impl *pimpl_in );
        void run();

    protected:
        void draw_controls() override;
        cataimgui::bounds get_bounds() override;
    private:
        /**
         * Set `selected_id` to `i` and according `msg`.
         */
        void set_selected_id( int i );
        std::string msg;
        acquire_graph_impl *pimpl;
};

std::string ItemN::name()
{
    return itm.display_name();
}

acquire_graph_impl::acquire_graph_impl()
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

    for( auto it = recipe_dict.begin(); it != recipe_dict.end(); ++it ) {
        const itype_id r_id = it->second.result();
        if( !crafting_result_count.count( r_id ) ) {
            crafting_result_count[r_id] = 0;
        }
        crafting_result_count[r_id] += 1;


        if( !from_crafting.count( r_id ) ) {
            from_crafting[r_id] = {};
        }
        from_crafting[r_id].emplace_back( it->first, crafting_source::product );

        for( /*std::map<itype_id, int>*/ auto const& [key, _] : it->second.get_byproducts() ) {
            if( !crafting_byproduct_count.count( key ) ) {
                crafting_byproduct_count[key] = 0;
            }
            crafting_byproduct_count[key] += 1;

            if( !from_crafting.count( key ) ) {
                from_crafting[key] = {};
            }
            from_crafting[key].emplace_back( it->first, crafting_source::byproduct );
        }
    }
}

void acquire_graph_impl::add_head( std::shared_ptr<AbstractN> head )
{
    graph_heads.emplace_back( head );
}

void acquire_graph_impl::add_item( const item &it )
{
    std::shared_ptr<ItemN> it_p = std::make_shared<ItemN>();
    it_p->itm = it;
    add_head( it_p );
}

std::vector<std::shared_ptr<AbstractN>>::iterator acquire_graph_impl::heads_begin()
{
    return graph_heads.begin();
}

std::vector<std::shared_ptr<AbstractN>>::iterator acquire_graph_impl::heads_end()
{
    return graph_heads.end();
}

inline int find_default( const std::map<const itype_id, int> &m, const itype_id &key,
                         int default_value = 0 )
{
    const auto &it = m.find( key );
    if( it != m.end() ) {
        return it->second;
    }
    return default_value;
}

acquire_graph_ui::acquire_graph_ui( acquire_graph_impl *pimpl_in )
    : cataimgui::window( _( "Acquire Graph" ) )
{
    pimpl = pimpl_in;
    // setup
    set_selected_id( pimpl->selected_id );
}

cataimgui::bounds acquire_graph_ui::get_bounds()
{
    return { -1.f, -1.f, float( str_width_to_pixels( TERMX ) ), float( str_height_to_pixels( TERMY ) ) };
}

void acquire_graph_ui::set_selected_id( int i )
{
    pimpl->selected_id = i;
    item ity( std::get<1>( pimpl->data_items[i] ), calendar::turn_zero );
    if( pimpl->from_crafting.count( ity.typeId() ) ) {
        msg = "";
        int recipe_n = 0;
        for( /*recipe_id, crafting_source*/ const auto& [r_id, cr_src]
                                            : pimpl->from_crafting[ity.typeId()] ) {
            msg += "\n";
            msg += cr_src == crafting_source::product ? "product" : "byproduct";
            msg += " of recipe ";
            msg += std::to_string( recipe_n++ );
            msg += " with requirements:\n";
            msg += r_id.obj().simple_requirements().list_all();
        }
        msg = _( string_format( "Craftable:%s", msg ) );
    } else {
        msg = _( "There are no sources for this item." );
    }

}

void acquire_graph_ui::draw_controls()
{
    ImGui::Text( "selected_id: %d", pimpl->selected_id );
    if( pimpl->selected_id != -1 && ImGui::Button( "Add Item Node" ) ) {
        item itm( std::get<1>( pimpl->data_items[pimpl->selected_id] ), calendar::turn_zero );
        pimpl->add_item( itm );
    }

    const float TEXT_BASE_HEIGHT = ImGui::GetTextLineHeightWithSpacing();
    ImVec2 outer_size = ImVec2( 0.0f, TEXT_BASE_HEIGHT * 20 );
    if( ! ImGui::BeginTable( "ACQUIRE_GRAPH", 4,
                             ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable
                             | ImGuiTableFlags_BordersOuter,
                             outer_size ) ) {
        return;
    }
    ImGui::TableSetupColumn( "?", 0, ImGui::CalcTextSize( "0" ).x );
    ImGui::TableSetupColumn( "itemo namae" );
    ImGui::TableSetupColumn( "itemo resuloto counto" );
    ImGui::TableSetupColumn( "itemo byproducoto counto" );
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
                set_selected_id( i );
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

            const itype_id &iid = std::get<1>( pimpl->data_items[i] )->get_id();

            ImGui::TableNextColumn();
            int count = find_default( pimpl->crafting_result_count, iid, 0 );
            ImGui::Text( "%d", count );

            ImGui::TableNextColumn();
            count = find_default( pimpl->crafting_byproduct_count, iid, 0 );
            ImGui::Text( "%d", count );
        }
    }
    ImGui::EndTable();


    std::string node_text = "Nodes selected:";
    for( auto head_it = pimpl->heads_begin(); head_it != pimpl->heads_end(); ++head_it ) {
        node_text += "\n";
        node_text += head_it->get()->name();
    }
    // For not jagging up when table leaves the screen (msg too long)
    ImGui::BeginChild( "Descriptions" );
    ImGui::TextWrapped( "%s", node_text.c_str() );
    ImGui::Separator();
    ImGui::TextWrapped( "%s", msg.c_str() );
    ImGui::EndChild();
}

void acquire_graph_ui::run()
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
        action = ctxt.handle_input( 5 );
        if( action == "QUIT" ) {
            break;
        }
    }
}

// These need to be here so that pimpl works with unique ptr
acquire_graph::acquire_graph() = default;
acquire_graph::~acquire_graph() = default;

void acquire_graph::show()
{
    if( pimpl == nullptr ) {
        pimpl = std::make_unique<acquire_graph_impl>();
    }
    acquire_graph_ui dui( pimpl.get() );
    dui.run();
}

bool acquire_graph::store()
{
    const std::string name = base64_encode( get_avatar().get_save_id() + "_acquire_graph" );
    cata_path path = PATH_INFO::world_base_save_path() +  "/" + name + ".json";
    const bool iswriten = write_to_file( path, [&]( std::ostream & fout ) {
        serialize( fout );
    }, _( "acquire_graph data" ) );
    return iswriten;
}

void acquire_graph::serialize( std::ostream &fout )
{
    JsonOut jsout( fout, true );
    jsout.start_object();
    serialize( jsout );
    jsout.end_object();
}

void acquire_graph::serialize( JsonOut &jsout )
{
    if( pimpl == nullptr ) {
        pimpl = std::make_unique<acquire_graph_impl>();
    }
    jsout.member( "selected_id", pimpl->selected_id );  // TODO scroll to it?
}

void acquire_graph::load()
{
    const std::string name = base64_encode( get_avatar().get_save_id() + "_acquire_graph" );
    cata_path path = PATH_INFO::world_base_save_path() / ( name + ".json" );
    if( file_exist( path ) ) {
        read_from_file_json( path, [&]( const JsonValue & jv ) {
            deserialize( jv );
        } );
    }
}

void acquire_graph::deserialize( const JsonValue &jsin )
{
    if( pimpl == nullptr ) {
        pimpl = std::make_unique<acquire_graph_impl>();
    }
    try {
        JsonObject data = jsin.get_object();

        data.read( "selected_id", pimpl->selected_id );
    } catch( const JsonError &e ) {

    }
}
