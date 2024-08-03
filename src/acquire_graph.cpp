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
#include "cata_assert.h"
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
#include "iteminfo_query.h"
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
#include "uistate.h"
#include "ui_manager.h"

// #pragma optimize( "", off )

static bool OPTIMIZE_AWAY_OR = true;

static ImGuiTableFlags flags = ImGuiTableFlags_BordersV | ImGuiTableFlags_BordersOuterH |
                               ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg | ImGuiTableFlags_NoBordersInBody;

//// see safemode &get_safemode()
//acquire_graph &get_acquire_graph()
//{
//    static acquire_graph single_instance;
//    return single_instance;
//}

struct AbstractN;


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
        friend struct AbstractN;
    public:
        void recalculate();
        void add_head( std::shared_ptr<AbstractN> &&head );
        void add_item( const item &it );

        std::vector<std::shared_ptr<AbstractN>> get_heads() {
            return graph_heads;
        }

        bool in_from_crafting( const itype_id r_id ) {
            return from_crafting.count( r_id );
        }
        std::vector<std::pair<recipe_id, crafting_source>> crafted_from( const itype_id r_id ) {
            cata_assert( in_from_crafting( r_id ) );
            return from_crafting.at( r_id );
        }
    private:
        int selected_id = -1;
        // TODO shared_ptr_fast?
        std::vector<std::shared_ptr<AbstractN>> graph_heads;
        /**
         * Data for item storage.
         */
        std::vector<std::tuple<std::string, const itype *, const itype_variant_data *>> data_items;
        std::map<const itype_id, int> crafting_result_count;
        std::map<const itype_id, int> crafting_byproduct_count;
        std::map<const itype_id, std::vector<itype_id>> disassembly_from;
        // TODO make the folowing faster?
        std::map<const itype_id, std::vector<std::pair<recipe_id, crafting_source>>> from_crafting;
};

struct AbstractN {
        virtual ~AbstractN() = default;
        // { return "ERROR: AbstractN NAME"; }; // TODO `=0` (delete) messes up emplace_back
        virtual std::string name() = 0;
        std::string get_expanded() const {
            return expanded ? "true" : "false";
        };
        // TODO return ref
        virtual std::vector<std::shared_ptr<AbstractN>> iterate_children() const = 0;
        /**
         * It has to have exactly one child in iterate_children.
         * Also some further condition may need to be satsfied.
         */
        virtual bool optimized_away() {
            return false;
        };
        /**
         * Expand this node, if not previousely expanded.
         */
        // TODO =0 (abstract)
        virtual void expand( acquire_graph_impl *pimpl ) {
            ( void )pimpl;
        };
    protected:
        bool expanded = false;

        // AbstractN *parent;
        /**
         * Node is satisfied. Parents can react to that. This is evaulatable.
         * Individual nodes maybe should store `bool is_satisfied` to remember it.
         * TODO: One must go!
         */
        //virtual bool satisfied();
        /**
         * Evals "something" and returns true, if this node is satisfied.
         * Should cache that result.
         * TODO: One must go!
         */
        //virtual bool eval_satisfied();
        //bool satisfied_cached = false;
};

// TODO maybe unnecessary level of abstraction, but it can be removed later
struct ObtainN : AbstractN {

    // obtain what? in what quantity? - TODO do we want that? - just look at parent for ID, no?
    // store quantity?
    // itype_id i_id;
};

/**
 * Obtain by crafting
 */
struct CraftN : ObtainN {
        explicit CraftN( const itype_id r_id_in ) : r_id( r_id_in ) {}
        std::string name() override {
            return "Obtain by crafting";
        }
        void expand( acquire_graph_impl *pimpl ) override;
        std::vector<std::shared_ptr<AbstractN>> iterate_children() const override {
            return viable_recipes;
        }
    private:
        // alternatively parent -> item -> get_id
        itype_id r_id;
        /**
         * Recipes having i_id as (by)product.
         */
        std::vector<std::shared_ptr<AbstractN>> viable_recipes;
};

/**
 * Item a person wants to obtain.
 * Also used in children of RecipeN. This fulfils requirements: components & tools.
 */
struct ItemN : AbstractN {
        // TODO make it from item to item_id
        ItemN( const item &itm_in, int count_in ) : itm( itm_in ), count( count_in ) {}
        std::string name() override {
            // TODO on tool, display_name(1), on item with charges, do something else too
            return itm.display_name( count ) + " " + std::to_string( count ) + " x";
        }
        std::vector<std::shared_ptr<AbstractN>> iterate_children() const override {
            return obtain_from;
        }
        void expand( acquire_graph_impl *pimpl ) override {
            if( expanded ) {
                return;
            }
            const itype_id r_id = itm.typeId();
            if( pimpl->in_from_crafting( r_id ) ) {
                std::shared_ptr<AbstractN> craft_p = std::make_shared<CraftN>( r_id );
                obtain_from.emplace_back( craft_p );
            }
            expanded = true;
        }
    private:
        item itm;
        // this can be number required in components, or in tools, TODO solve for -1
        int count;
        /**
         * Children are in OR relation (at least one must be satisfied).
         */
        // TODO craft / butcher / smash / deconstruct / find
        std::vector<std::shared_ptr<AbstractN>> obtain_from;
};

/*TODO instead of these:
Set count in every ItemN
TODO describe tool_groups, component_groups as a single AndNode
    containing (AndNode for tools, AndNode for components, ...)
Add std::string `label` to AndN to list what is Anding on, then we can do:
    Item smth
        Obtainable from crafting
            Recipe 1
                And Node recipe Requirements
                    And Node Tools
                        Or node - optimize away?
                            nearby fire (-1) -> nearby fire
                        Or node
                            cooker (50)
                        Or Node
                            electric needle (50)
                            sewing set
                    And Node Components
                        Or Node
                            50 thread
                            50 sinew

// TODO optimize away nodes that have only one child in some cases (And Or)
*/

/*
struct ComponentN : ItemN {};
struct ToolN : ItemN {};
*/

struct OrN : AbstractN {
        explicit OrN( const std::vector<std::shared_ptr<AbstractN>> &items_in ) : items( items_in ) {
            expanded = true;
        }
        std::string name() override {
            //if( optimized_away() ) {
            //    return items.front().first->name();
            //}
            return "Or Node";
        }
        bool optimized_away() override {
            return OPTIMIZE_AWAY_OR && items.size() == 1;
        };
        std::vector<std::shared_ptr<AbstractN>> iterate_children() const override {
            //if( optimized_away() ) {
            //    return items.front()->iterate_children();
            //}
            return items;
        }
    private:
        /**
         *  Satisfied if all childrens are satisfied.
         */
        std::vector<std::shared_ptr<AbstractN>> items;
};

struct AndN : AbstractN {
        AndN( const std::string &label_in,
              const std::vector<std::shared_ptr<AbstractN>> &items_in ) : label( label_in ), items( items_in ) {
            expanded = true;
        }
        std::string name() override {
            return "And Node " + label;
        }
        std::vector<std::shared_ptr<AbstractN>> iterate_children() const override {
            return items;
        }
        /**
         *  Satisfied if any children is satisfied.
         */
    private:
        std::string label;
        std::vector<std::shared_ptr<AbstractN>> items;
};

/**
 * Node for recipe.
 * We need to Handle multiple results, since we might want all of them
 */
struct RecipeN : AbstractN {
        RecipeN( const recipe_id r_id_in, const std::string &by_product_in ) : r_id( r_id_in ),
            by_product( by_product_in ) {}
        std::string name() override {
            // TODO this is probably getting slow, cache it?
            return by_product + " of a recipe for " + r_id.obj().result().obj().nname( 1 );
        }
        // TODO tools, components, ??
        // TODO counts
        void expand( acquire_graph_impl *pimpl ) override {
            ( void )pimpl;
            // recipe -> requirements -> get_tools
            if( expanded ) {
                return;
            }
            requirement_data reqs = r_id.obj().simple_requirements();
            tool_groups = std::make_shared<AndN>( "Tools", component_to_node( reqs.get_tools() ) );
            component_groups = std::make_shared<AndN>( "Components",
                               component_to_node( reqs.get_components() ) );
            expanded = true;
        }
        std::vector<std::shared_ptr<AbstractN>> iterate_children() const override {
            std::vector<std::shared_ptr<AbstractN>> ret;
            // TODO this could probably crash
            // Water, 3rd byproduct has empty tools
            if( !tool_groups->iterate_children().empty() ) {
                ret.emplace_back( tool_groups );
            }
            // TODO can it ever be empty?
            ret.emplace_back( component_groups );
            return ret;
        }
    private:
        template <typename T> std::vector<std::shared_ptr<AbstractN>> component_to_node(
                    const std::vector<std::vector<T>> &items
        ) {
            std::vector<std::shared_ptr<AbstractN>> AND_node;
            for( const std::vector<T> &req_and : items ) {
                std::vector<std::shared_ptr<AbstractN>> OR_node;
                for( const T &req_or : req_and ) {
                    item itm( req_or.type, calendar::turn_zero );
                    OR_node.emplace_back( std::make_shared<ItemN>( itm, req_or.count ) );
                }
                AND_node.emplace_back( std::make_shared<OrN>( OR_node ) );
            }
            return AND_node;
        }
        recipe_id r_id;
        std::string by_product;
        /**
         * Products and byproducts this produces, and their count
         */
        //std::vector<std::pair<itype, int>> results;
        /**
         * Tools aren't consumed.
         * TODO: but maybe their charges can be consumed?
         */
        std::shared_ptr<AbstractN> tool_groups;
        /**
         * Components are consumed.
         */
        std::shared_ptr<AbstractN> component_groups;
        //int time;
};

///**
// * Obtain by finding (in some location).
// */
//struct FindN : ObtainN {
//    /**
//     * Locations where i_id can be found.
//     */
//    std::vector<int /*location_id*/> viable_locations;
//};
//
///**
// * Hand written (custom) node that can have custom requirements to be satisfied.
// * The AND and OR are the useful ones, since this one couldn't have children.
// * But CustomN can be note: "Left amazing boots in town, go get them at x:y:z"
// * Anything could be created from the AND OR custom nodes.
// */
//struct CustomN : AbstractN {
//};
//
//struct CustomAndN : AbstractN {
//    std::vector<std::shared_ptr<AbstractN>> children_and;
//};
//
//struct CustomOrN : AbstractN {
//    std::vector<std::shared_ptr<AbstractN>> children_or;
//};

class acquire_graph_ui : public cataimgui::window
{
    public:
        explicit acquire_graph_ui( acquire_graph_impl *pimpl_in );
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

void CraftN::expand( acquire_graph_impl *pimpl )
{
    if( expanded ) {
        return;
    }
    for( auto const& /*recipe_id, crafting_source*/[c_r_id, c_src] : pimpl->crafted_from( r_id ) ) {
        std::shared_ptr<AbstractN> craft_p = std::make_shared<RecipeN>( c_r_id,
                                             c_src == crafting_source::product ? "product" : "byproduct" );
        viable_recipes.emplace_back( craft_p );
    }
    expanded = true;
}

void acquire_graph_impl::recalculate()
{
    // data_items
    std::vector<std::tuple<std::string, const itype *, const itype_variant_data *>> opts;
    for( const itype *i : item_controller->all() ) {
        // player didn't see this item yet, don't spoil it!
        // TODO: should I just iterate over read_items?
        // TODO: debug - read all items

        // TODO: doesn't work, data_items is empty now.
        if( !uistate.read_items.count( i->get_id() ) ) {
            continue;
        }
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
        // DISASSEMVLY
        //disassembly_from
        // iterate known items
        //i.disassemble
        // add to vector
    }
    std::sort( opts.begin(), opts.end(), localized_compare );
    data_items = opts;

    // from_crafting OLD & NEW
    // TODO: only recipes that player has seen.
    // TODO: remember recipes in a new var `recipes_read`.
    //       Otherwise, player would construct a graph that would miss nodes later
    for( const auto& [r_id, rec] : recipe_dict ) {
        // TODO: ?ref
        const itype_id iid = rec.result();
        if( !crafting_result_count.count( iid ) ) {
            crafting_result_count[iid] = 0;
        }
        crafting_result_count[iid] += 1;


        if( !from_crafting.count( iid ) ) {
            from_crafting[iid] = {};
        }
        from_crafting[iid].emplace_back( r_id, crafting_source::product );

        for( /*std::map<itype_id, int>*/ auto const& [key, _] : rec.get_byproducts() ) {
            if( !crafting_byproduct_count.count( key ) ) {
                crafting_byproduct_count[key] = 0;
            }
            crafting_byproduct_count[key] += 1;

            if( !from_crafting.count( key ) ) {
                from_crafting[key] = {};
            }
            from_crafting[key].emplace_back( r_id, crafting_source::byproduct );
        }
    }
}

void acquire_graph_impl::add_head( std::shared_ptr<AbstractN> &&head )
{
    graph_heads.emplace_back( std::move( head ) );
}

void acquire_graph_impl::add_item( const item &itm )
{
    add_head( std::make_shared<ItemN>( itm, 1 ) );
}

static int find_default( const std::map<const itype_id, int> &m, const itype_id &key,
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
    // TODO selected it is wrong, when NEW read_item is added
    if( i >= static_cast<int>( pimpl->data_items.size() ) ) {
        i = -1;
    }
    pimpl->selected_id = i;
    if( i == -1 ) {   // First load
        msg = _( "No recipe selected." );
        return;
    }
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

// TODO: it is confusing that row_node is const, but underlying AbstractN is changed
static void show_table_rec( const std::shared_ptr<AbstractN> &row_node, acquire_graph_impl *pimpl )
{
    if( row_node->optimized_away() ) {
        show_table_rec( row_node->iterate_children().front(), pimpl );
        return;
    }
    ImGui::TableNextColumn();
    // TODO this hopefully converst the pointer to a unique ID
    // https://stackoverflow.com/a/76066361/5057078
    // after ## add pointer to the Node this is based on as a unique ID
    const std::string &unique_id = std::to_string( reinterpret_cast< unsigned long long >
                                   ( reinterpret_cast<void **>( row_node.get() ) ) );
    bool open = ImGui::TreeNodeEx( ( row_node->name() + "##" + unique_id ).c_str() );

    ImGui::TableNextColumn();
    ImGui::Text( "%s", row_node->get_expanded().c_str() );
    if( open ) {
        row_node->expand( pimpl );
        for( const std::shared_ptr<AbstractN> &child : row_node->iterate_children() ) {
            show_table_rec( child, pimpl );
        }
        ImGui::TreePop();
    }
}

static void show_table( acquire_graph_impl *pimpl )
{
    const float TEXT_BASE_WIDTH = ImGui::CalcTextSize( "A" ).x;

    ImGui::Checkbox( "OPTIMIZE_AWAY_OR", &OPTIMIZE_AWAY_OR );
    if( !ImGui::BeginTable( "mything", 2, flags ) ) {
        return;
    }
    ImGui::TableSetupColumn( "Name", ImGuiTableColumnFlags_NoHide );
    ImGui::TableSetupColumn( "Expanded", ImGuiTableColumnFlags_WidthFixed, TEXT_BASE_WIDTH * 18.0f );
    ImGui::TableHeadersRow();

    for( const std::shared_ptr<AbstractN> &head : pimpl->get_heads() ) {
        show_table_rec( head, pimpl );
    }

    ImGui::EndTable();
}

void acquire_graph_ui::draw_controls()
{
    // const float TEXT_BASE_WIDTH = ImGui::CalcTextSize( "A" ).x;

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
    // TODO: ( "?", 0, ImGui::CalcTextSize( "0" ).x );
    ImGui::TableSetupColumn( "?"/*, 0, TEXT_BASE_WIDTH*/ );
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

            cataimgui::draw_colored_text( ity.symbol(), ity.color() );

            std::string i_name = std::get<0>( pimpl->data_items[i] );
            if( std::get<2>( pimpl->data_items[i] ) != nullptr ) {
                i_name += "<color_dark_gray>(V)</color>";
            }
            if( !std::get<1>( pimpl->data_items[i] )->snippet_category.empty() ) {
                i_name += "<color_yellow>(S)</color>";
            }

            ImGui::TableNextColumn();
            cataimgui::draw_colored_text( i_name, c_white );

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

    // For not jagging up when table leaves the screen (msg too long)
    ImGui::BeginChild( "Descriptions" );
    show_table( pimpl );
    //cataimgui::draw_colored_text( msg.c_str(), c_white );
    ImGui::TextWrapped( "%s", msg.c_str() );

    // SHOW SELECTED ITEM's disassembly
    // TODO: other way around - show all items with this as disassembly
    if( pimpl->selected_id != -1 ) {
        std::vector<iteminfo_parts> disassemble = { iteminfo_parts::DESCRIPTION_COMPONENTS_DISASSEMBLE };

        item itm( std::get<1>( pimpl->data_items[ pimpl->selected_id ] ), calendar::turn_zero );

        std::vector<iteminfo> info_v;
        const iteminfo_query query_v( disassemble );
        itm.info( info_v, &query_v, 1 );

        const std::string &res = format_item_info( info_v, {} );
        if( !res.empty() ) {
            ImGui::TextWrapped( "%s", res.c_str() );
        } else {
            ImGui::TextWrapped( "%s", _( "Item doesn't disassemble." ) );
        }
    }

    ImGui::EndChild();
}

void acquire_graph_ui::run()
{
    // TODO init things
    pimpl->recalculate();
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
acquire_graph::acquire_graph() = default;
acquire_graph::~acquire_graph() = default;

void acquire_graph::show()
{
    if( pimpl == nullptr ) {
        pimpl = std::make_unique<acquire_graph_impl>();
    }
    //bool open_metrics = true;
    //ImGui::ShowMetricsWindow(/*&open_metrics*/);
    //open_metrics = false;
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

// #pragma optimize( "", on )
