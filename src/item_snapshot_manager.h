#pragma once
#ifndef CATA_SRC_ITEM_SNAPSHOT_MANAGER_H
#define CATA_SRC_ITEM_SNAPSHOT_MANAGER_H

#include <iosfwd>
#include <memory>

class JsonOut;
class JsonValue;
class item_snapshot_manager_impl;

class item_snapshot_manager
{
    public:
        item_snapshot_manager();
        ~item_snapshot_manager();
        /*serialize and deserialize*/
        bool store();
        void load();
        void serialize( std::ostream &fout );
        void deserialize( const JsonValue &jsin );
        void serialize( JsonOut &jsout );

        void show();
    private:
        std::unique_ptr <item_snapshot_manager_impl> pimpl;
};

#endif // CATA_SRC_ITEM_SNAPSHOT_MANAGER_H
