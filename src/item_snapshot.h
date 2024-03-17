#pragma once
#ifndef CATA_SRC_ITEM_SNAPSHOT_H
#define CATA_SRC_ITEM_SNAPSHOT_H

#include <iosfwd>
#include <memory>

class JsonOut;
class JsonValue;
class item_snapshot_impl;

class item_snapshot
{
    public:
        item_snapshot();
        ~item_snapshot();
        /*serialize and deserialize*/
        bool store();
        void load();
        void serialize( std::ostream &fout );
        void deserialize( const JsonValue &jsin );
        void serialize( JsonOut &jsout );

        void show();
    private:
        std::unique_ptr <item_snapshot_impl> pimpl;
};

#endif // CATA_SRC_ITEM_SNAPSHOT_H
