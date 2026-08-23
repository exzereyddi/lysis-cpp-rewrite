#pragma once

#include "nodes.hpp"

namespace lysis {

    namespace NodeAnalysis {
        void RemoveGuards(NodeGraph& graph);
        void RemoveDeadCode(NodeGraph& graph);
        void CollapseArrayReferences(NodeGraph& graph);
        void CoalesceLoadStores(NodeGraph& graph);
        void CoalesceLoadsAndDeclarations(NodeGraph& graph);
        void HandleMemCopys(NodeGraph& graph);
        void AnalyzeHeapUsage(NodeGraph& graph);
    }

}