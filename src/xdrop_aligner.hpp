
/**
 * @file xdrop_aligner.hpp
 * @author Hajime Suzuki
 * @date 2018/03/23
 */
#ifndef VG_XDROP_ALIGNER_HPP_INCLUDED
#define VG_XDROP_ALIGNER_HPP_INCLUDED

#include <algorithm>
#include <cstdint>			/* int8_t, ... */
#include <functional>
#include <unordered_map>
#include <vector>

#include <vg/vg.pb.h>
#include "types.hpp"
#include "handle.hpp"
#include "mem.hpp"

// #define BENCH
// #include "bench.h"

struct dz_s;
struct dz_forefront_s;
struct dz_query_s;

namespace vg {

    /**
     * Align to a graph using the xdrop algorithm, as implemented in dozeu.
     *
     * Not thread-safe. Each align() call stores state in the object.
     *
     * *Can* be re-used for multiple problems in a row.
     *
     * The underlying Dozeu library is fundamentally based around semi-global
     * alignment: extending an alignment from a known matching position (what
     * in other parts of vg we call "pinned" alignment).
     *
     * To simulate non-pinned alignment, we align in two passes in different
     * directions. One from a guess of a pinning position, to get a more
     * accurate "head" pinning position for the other end, and once back from
     * where the previous pass ended up, to get an overall hopefully-optimal
     * alignment.
     *
     * If the input graph is not reverse-complemented, direction = false
     * (reverse, right to left) on the first pass, and direction = true
     * (forward, left to right) on the second. If it is reverse complemented,
     * we flip them.
     * 
     * This won't actually work in theory to get the optimal local alignment in
     * all cases, but it works well in practice.
     *
     */
	class XdropAligner {
    public:
    
        /**
         * Represents a correspondance between a position in the subgraph we are
         * mapping to and a position in the read we are mapping.
         */
        struct graph_pos_s {
            /// What index in the node list of our extracted subgraph is our node at?
            size_t node_index;
            /// What is the offset in the node? Note that we only think about the forward strand.
            uint32_t ref_offset;
            /// What is the correspondign offset in the query sequence?
            uint32_t query_offset;
        };
    
        /**
         * Represents a HandleGraph with a defined (topological) order calculated for it.
         *
         * Meant to be used with aggregate initialization ({wrapper, order}).
         */
        struct OrderedGraph {
            HandleGraph const &graph;
            vector<handle_t> const &order;
        };
        
	private:
		// context (contains memory arena and constants) and working buffers
        
        /// This is the backing dozeu library problem instance
		dz_s *dz;
        
        // We don't need to remember the whole score matrix, because dz has a
        // copy. But we do need the AA match score to support pinned alignment.
        int8_t aa_match;
        
        /// Maps from node ID to the index in our internal subgraph storage at which that node occurs
        // can be lighter? index in lower 32bit and graph_id -> mem_id mapping (inverse of trans mapping)
		std::unordered_map< id_t, uint64_t > id_to_index;
        
        
        /// List of edges. Stored as two int32_ts packed together.
        /// TODO: what is the order of packing?
        // (int32_t, int32_t) tuple; FIXME: index_edges and index_edges_head are partly duplicated
		std::vector< uint64_t > index_edges;
        /// TODO: what is this?
        std::vector< uint64_t > index_edges_head;
        
        /// Stores all of the currently outstanding dozeu library forefronts.
		std::vector< struct dz_forefront_s const * > forefronts;

        
        /// Lookup table for forward- and reverse-sorting comparators, interpreting unsigned arguments as signed.
        /// Use [0] for forward and [1] for reverse (FIXME: can we embed them in the vtable?)
		std::function<bool (uint64_t const &, uint64_t const &)> const compare[2] = {
			[](uint64_t const &x, uint64_t const &y) -> bool { return((int64_t)x < (int64_t)y); },
			[](uint64_t const &x, uint64_t const &y) -> bool { return((int64_t)x > (int64_t)y); }
		};
        
        /// Lookup table for functions to compose packed edge uint64_t values from the from and to indexes.
        /// Use [0] to put to in the high bits and [1] to put from in the high bits.
        /// TODO: When would you use either?
		std::function<uint64_t (uint64_t const &, uint64_t const &)> const edge[2] = {
			[](uint64_t const &from, uint64_t const &to) -> uint64_t { return((to<<32) | from); },
			[](uint64_t const &from, uint64_t const &to) -> uint64_t { return((from<<32) | to); }
		};

		// working buffer init functions
        
        /// Fill in the id_to_index map
		void build_id_index_table(OrderedGraph const &graph);
        
        
        
        /// Fill in index_edges and index_edges_head. Needs to know the index
        /// of the "seed node" in our graph's list of nodes, and the direction
        /// of the pass we are setting up for (false = right to left, true = left to right) 
		void build_index_edge_table(OrderedGraph const &graph, uint32_t const seed_node_index, bool left_to_right);

		// position handling -> (node_index, ref_offset, query_offset): graph_pos_s
		// MaximalExactMatch const &select_root_seed(vector<MaximalExactMatch> const &mems);
        
        /// Given the subgraph we are aligning to, the MEM hist against it, the
        /// length of the query, and the direction we are aligning the query in
        /// (true = forward), select a single anchoring match between the graph
        /// and the query to align out from.
        ///
        /// This replaces scan_seed_position for the case where we have MEMs.
		graph_pos_s calculate_seed_position(OrderedGraph const &graph, vector<MaximalExactMatch> const &mems, size_t query_length, bool direction);
        /// Given the index of the node at which the winning score occurs, find
        /// the position in the node and read sequence at which the winning
        /// match is found.
        graph_pos_s calculate_max_position(OrderedGraph const &graph, graph_pos_s const &seed_pos, size_t max_node_index, bool direction);
	
        /// If no seeds are provided as alignment input, we need to compute our own starting anchor position. This function does that.
        /// Takes the topologically-sorted graph, the query sequence, and the direction.
        /// If direction is false, finds a seed hit on the first node of the graph. If it is true, finds a hit on the last node.
        ///
        /// This replaces calculate_seed_position for the case where we have no MEMs.
        graph_pos_s scan_seed_position(OrderedGraph const &graph, std::string const &query_seq, bool direction);

        /// Append an edit at the end of the current mapping array.
        /// Returns the length passed in.
		size_t push_edit(Mapping *mapping, uint8_t op, char const *alt, size_t len);

		// extension -> max_node_index: size_t
        
        /// Do alignment. Takes the graph, the sorted packed edges in
        /// ascending order for a forward pass or descending order for a
        /// reverse pass, the packed query sequence, the index of the seed node
        /// in the graph, the offset (TODO: in the read?) of the seed position,
        /// and the direction to traverse the graph topological order.
        ///
        /// Note that we take our direction as right_to_left, whole many other
        /// functions take it as left_to_right.
        ///
        /// If a MEM seed is provided, this is run in two passes. The first is
        /// left to right (right_to_left = false) if align did not have
        /// reverse_complement set and the second is right to left (right_to_left =
        /// true).
        ///
        /// If we have no MEM seed, we only run one pass (the second one).
        ///
        /// Returns the index in the topological order of the node with the
        /// highest scoring alignment.
        ///
        /// Note that if no non-empty local alignment is found, it may not be
        /// safe to call dz_calc_max_qpos on the associated forefront!
		size_t extend(OrderedGraph const &graph, vector<uint64_t>::const_iterator begin, vector<uint64_t>::const_iterator end, dz_query_s const *packed_query, size_t seed_node_index, uint64_t seed_offset, bool right_to_left);
       
        /**
         * After all the alignment work has been done, do the traceback and
         * save into the given Alignment object.
		 *
         * If left_to_right is true, the nodes were filled left to right, and
         * the internal traceback will come out in left to right order, so we
         * can emit it as is. If it is false, the nodes were filled right to
         * left, and the internal traceback comes out in right to left order,
         * so we need to flip it.
         */
        void calculate_and_save_alignment(Alignment &alignment, OrderedGraph const &graph, graph_pos_s const &head_pos, size_t tail_node_index, bool left_to_right);

		// void debug_print(Alignment const &alignment, OrderedGraph const &graph, MaximalExactMatch const &seed, bool reverse_complemented);
		// bench_t bench;
        
        /// After doing the upward pass and finding head_pos to anchor from, do
        /// the downward alignment pass and traceback. If left_to_right is
        /// set, goes left to right and traces back the other way. If it is
        /// unset, goes right to left and traces back the other way.
        void align_downward(Alignment &alignment, OrderedGraph const &graph, graph_pos_s const &head_pos, bool left_to_right);

	public:
		// default_* defined in vg::, see aligner.hpp
		XdropAligner();
		XdropAligner(XdropAligner const &);
		XdropAligner& operator=(XdropAligner const &);
		XdropAligner(XdropAligner&&);
		XdropAligner& operator=(XdropAligner&&);
		XdropAligner(int8_t _match,
			int8_t _mismatch,
			int8_t _gap_open,
			int8_t _gap_extension,
			int32_t _full_length_bonus,
			uint32_t _max_gap_length);
		XdropAligner(int8_t const *_score_matrix,
			int8_t _gap_open,
			int8_t _gap_extension,
			int32_t _full_length_bonus,
			uint32_t _max_gap_length);
		~XdropAligner(void);
        
        /**
         * align query: forward-backward banded alignment
         *
         * Compute an alignment of the given Alignment's sequence against the
         * given topologically sorted graph, using (one of) the given MEMs to
         * seed the alignment.
         *
         * reverse_complemented is true if the topologically sorted graph we
         * have was reverse-complemented when extracted from a larger
         * containing graph, and false if it is in the same orientation as it
         * exists in the larger containing graph. The MEMs and the Alignment
         * are interpreted as being against the forward strand of the passed
         * subgraph no matter the value of this setting.
         *
         * reverse_complemented true means we will compute the alignment
         * forward in the topologically-sorted order of the given graph
         * (anchoring to the first node if no MEMs are provided) and false if
         * we want to compute the alignment backward in the topological order
         * (anchoring to the last node).
         *
         * All the graph edges must go from earlier to later nodes, and
         * from_start and to_end must alsways be false.
         *
         * First the head (the most upstream) seed in MEMs is selected and
         * extended downward to detect the downstream breakpoint. Next the
         * alignment path is generated by second upward extension from the
         * downstream breakpoint.
         *
         * The MEM list may be empty. If MEMs are provided, uses only the
         * begin, end, and nodes fields of the MaximalExactMatch objects. It
         * uses the first occurrence of the last MEM if reverse_complemented is
         * true, and the last occurrence of the first MEM otherwise.
         */
        void align(Alignment &alignment, OrderedGraph const &graph, const vector<MaximalExactMatch> &mems, bool reverse_complemented);
        
        /// Implementation of align() that automatically wraps up a topologically-ordered Protobuf graph as an OrderedGraph.
        void align(Alignment &alignment, Graph const &graph, const vector<MaximalExactMatch> &mems, bool reverse_complemented);
        
        /**
         * Compute a pinned alignment, where the start (pin_left=true) or end
         * (pin_left=false) end of the Alignment sequence is pinned to the
         * start of the first (pin_left=true) or end of the last
         * (pin_left=false) node in the graph's topological order.
         *
         * Does not account for multiple sources/sinks in the topological
         * order; whichever comes first/last ends up being used for the pin.
         *
         * TODO: This should become const and the class should become thread safe.
         */
        void align_pinned(Alignment& alignment, const HandleGraph& g, bool pin_left);
        
        /// Version of align_pinned that allows you to pass your own topological order.
        /// The topological order MUST be left to right, no matter whether you are pinning left or right.
        /// If alignment needs to proceed backward, it will be reversed internally.
        /// TODO: This should become const and the class should become thread safe.
        void align_pinned(Alignment& alignment, const HandleGraph& g, const vector<handle_t>& topological_order,
                          bool pin_left);
	};
} // end of namespace vg

#endif
/**
 * end of xdrop_aligner.hpp
 */
