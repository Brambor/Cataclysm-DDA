#pragma once
#ifndef CATA_SRC_ACQUIRE_GRAPH_H
#define CATA_SRC_ACQUIRE_GRAPH_H

#include <iosfwd>
#include <memory>

class JsonOut;
class JsonValue;
class acquire_graph_impl;

class acquire_graph
{
    public:
        acquire_graph();
        ~acquire_graph();
        /*serialize and deserialize*/
        bool store();
        void load();
        void serialize( std::ostream &fout );
        void deserialize( const JsonValue &jsin );
        void serialize( JsonOut &jsout );

        void show();
    private:
        std::unique_ptr <acquire_graph_impl> pimpl;
};

#endif // CATA_SRC_ACQUIRE_GRAPH_H
