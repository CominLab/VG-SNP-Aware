#include "subcommand.hpp"
#include "../vg.hpp"
#include "../xg.hpp"
#include "../utility.hpp"
#include "../mapper.hpp"
#include <vg/io/stream.hpp>
#include <vg/io/vpkg.hpp>
#include <vg/io/protobuf_emitter.hpp>
#include <gbwt/gbwt.h>
#include "../region.hpp"
#include "../stream_index.hpp"
#include "../algorithms/sorted_id_ranges.hpp"
#include "../algorithms/approx_path_distance.hpp"
#include "../algorithms/walk.hpp"
#include <bdsg/overlays/overlay_helper.hpp>

#include <unistd.h>
#include <getopt.h>

using namespace vg;
using namespace vg::subcommand;
using namespace vg::io;

void help_find(char** argv) {
    cerr << "usage: " << argv[0] << " find [options] >sub.vg" << endl
         << "options:" << endl
         << "    -d, --db-name DIR      use this db (defaults to <graph>.index/)" << endl
         << "    -x, --xg-name FILE     use this xg index or graph (instead of rocksdb db)" << endl
         << "graph features:" << endl
         << "    -n, --node ID          find node(s), return 1-hop context as graph" << endl
         << "    -N, --node-list FILE   a white space or line delimited list of nodes to collect" << endl
         << "    -e, --edges-end ID     return edges on end of node with ID" << endl
         << "    -s, --edges-start ID   return edges on start of node with ID" << endl
         << "    -c, --context STEPS    expand the context of the subgraph this many steps" << endl
         << "    -L, --use-length       treat STEPS in -c or M in -r as a length in bases" << endl
         << "    -P, --position-in PATH find the position of the node (specified by -n) in the given path" << endl
         << "    -I, --list-paths       write out the path names in the index" << endl
         << "    -r, --node-range N:M   get nodes from N to M" << endl
         << "    -G, --gam GAM          accumulate the graph touched by the alignments in the GAM" << endl
         << "subgraphs by path range:" << endl
         << "    -p, --path TARGET      find the node(s) in the specified path range(s) TARGET=path[:pos1[-pos2]]" << endl
         << "    -R, --path-bed FILE    read our targets from the given BED FILE" << endl
         << "    -E, --path-dag         with -p or -R, gets any node in the partial order from pos1 to pos2, assumes id sorted DAG" << endl
         << "    -W, --save-to PREFIX   instead of writing target subgraphs to stdout," << endl
         << "                           write one per given target to a separate file named PREFIX[path]:[start]-[end].vg" << endl
         << "    -K, --subgraph-k K     instead of graphs, write kmers from the subgraphs" << endl
         << "    -H, --gbwt FILE        when enumerating kmers from subgraphs, determine their frequencies in this GBWT haplotype index" << endl
         << "alignments:" << endl
         << "    -d, --db-name DIR      use this RocksDB database to retrieve alignments" << endl
         << "    -l, --sorted-gam FILE  use this sorted, indexed GAM file" << endl
         << "    -a, --alignments       write all alignments from input sorted GAM or RocksDB" << endl
         << "    -o, --alns-on N:M      write alignments which align to any of the nodes between N and M (inclusive)" << endl
         << "    -A, --to-graph VG      get alignments to the provided subgraph" << endl
         << "sequences:" << endl
         << "    -g, --gcsa FILE        use this GCSA2 index of the sequence space of the graph" << endl
         << "    -z, --kmer-size N      split up --sequence into kmers of size N" << endl
         << "    -j, --kmer-stride N    step distance between successive kmers in sequence (default 1)" << endl
         << "    -S, --sequence STR     search for sequence STR using --kmer-size kmers" << endl
         << "    -M, --mems STR         describe the super-maximal exact matches of the STR (gcsa2) in JSON" << endl
         << "    -B, --reseed-length N  find non-super-maximal MEMs inside SMEMs of length at least N" << endl
         << "    -f, --fast-reseed      use fast SMEM reseeding algorithm" << endl
         << "    -Y, --max-mem N        the maximum length of the MEM (default: GCSA2 order)" << endl
         << "    -Z, --min-mem N        the minimum length of the MEM (default: 1)" << endl
         << "    -k, --kmer STR         return a graph of edges and nodes matching this kmer" << endl
         << "    -T, --table            instead of a graph, return a table of kmers" << endl
         << "                           (works only with kmers in the index)" << endl
         << "    -C, --kmer-count       report approximate count of kmer (-k) in db" << endl
         << "    -D, --distance         return distance on path between pair of nodes (-n). if -P not used, best path chosen heurstically" << endl
         << "    -Q, --paths-named S    return all paths whose names are prefixed with S (multiple allowed)" << endl;

}

int main_find(int argc, char** argv) {

    if (argc == 2) {
        help_find(argv);
        return 1;
    }

    string db_name;
    string sequence;
    int kmer_size=0;
    int kmer_stride = 1;
    vector<string> kmers;
    vector<vg::id_t> node_ids;
    string node_list_file;
    int context_size=0;
    bool use_length = false;
    bool count_kmers = false;
    bool kmer_table = false;
    vector<string> targets_str;
    vector<Region> targets;
    string path_name;
    bool position_in = false;
    string range;
    string gcsa_in;
    string xg_name;
    bool get_mems = false;
    int mem_reseed_length = 0;
    bool use_fast_reseed = true;
    string sorted_gam_name;
    bool get_alignments = false;
    bool get_mappings = false;
    string aln_on_id_range;
    vg::id_t start_id = 0;
    vg::id_t end_id = 0;
    bool pairwise_distance = false;
    string gam_file;
    int max_mem_length = 0;
    int min_mem_length = 1;
    string to_graph_file;
    bool extract_threads = false;
    vector<string> extract_thread_patterns;
    bool extract_paths = false;
    vector<string> extract_path_patterns;
    bool list_path_names = false;
    bool path_dag = false;
    string bed_targets_file;
    string save_to_prefix;
    int subgraph_k = 0;
    string gbwt_name;

    int c;
    optind = 2; // force optind past command positional argument
    while (true) {
        static struct option long_options[] =
            {
                //{"verbose", no_argument,       &verbose_flag, 1},
                {"db-name", required_argument, 0, 'd'},
                {"xg-name", required_argument, 0, 'x'},
                {"gcsa", required_argument, 0, 'g'},
                {"node", required_argument, 0, 'n'},
                {"node-list", required_argument, 0, 'N'},
                {"edges-end", required_argument, 0, 'e'},
                {"edges-start", required_argument, 0, 's'},
                {"kmer", required_argument, 0, 'k'},
                {"table", no_argument, 0, 'T'},
                {"sequence", required_argument, 0, 'S'},
                {"mems", required_argument, 0, 'M'},
                {"reseed-length", required_argument, 0, 'B'},
                {"fast-reseed", no_argument, 0, 'f'},
                {"kmer-stride", required_argument, 0, 'j'},
                {"kmer-size", required_argument, 0, 'z'},
                {"context", required_argument, 0, 'c'},
                {"use-length", no_argument, 0, 'L'},
                {"kmer-count", no_argument, 0, 'C'},
                {"path", required_argument, 0, 'p'},
                {"path-bed", required_argument, 0, 'R'},
                {"path-dag", no_argument, 0, 'E'},
                {"save-to", required_argument, 0, 'W'},
                {"position-in", required_argument, 0, 'P'},
                {"node-range", required_argument, 0, 'r'},
                {"sorted-gam", required_argument, 0, 'l'},
                {"alignments", no_argument, 0, 'a'},
                {"mappings", no_argument, 0, 'm'},
                {"alns-on", required_argument, 0, 'o'},
                {"distance", no_argument, 0, 'D'},
                {"gam", required_argument, 0, 'G'},
                {"to-graph", required_argument, 0, 'A'},
                {"max-mem", required_argument, 0, 'Y'},
                {"min-mem", required_argument, 0, 'Z'},
                {"paths-named", required_argument, 0, 'Q'},
                {"list-paths", no_argument, 0, 'I'},
                {"subgraph-k", required_argument, 0, 'K'},
                {"gbwt", required_argument, 0, 'H'},
                {0, 0, 0, 0}
            };

        int option_index = 0;
        c = getopt_long (argc, argv, "d:x:n:e:s:o:k:hc:LS:z:j:CTp:P:r:l:amg:M:B:fDG:N:A:Y:Z:IQ:ER:W:K:H:",
                         long_options, &option_index);

        // Detect the end of the options.
        if (c == -1)
            break;

        switch (c)
        {
        case 'd':
            db_name = optarg;
            break;

        case 'x':
            xg_name = optarg;
            break;

        case 'g':
            gcsa_in = optarg;
            break;

        case 'k':
            kmers.push_back(optarg);
            break;

        case 'S':
            sequence = optarg;
            break;

        case 'M':
            sequence = optarg;
            get_mems = true;
            break;
            
        case 'B':
            mem_reseed_length = parse<int>(optarg);
            break;
            
        case 'f':
            use_fast_reseed = true;
            break;

        case 'Y':
            max_mem_length = parse<int>(optarg);
            break;
            
        case 'Z':
            min_mem_length = parse<int>(optarg);
            break;
            
        case 'j':
            kmer_stride = parse<int>(optarg);
            break;

        case 'z':
            kmer_size = parse<int>(optarg);
            break;

        case 'C':
            count_kmers = true;
            break;

        case 'p':
            targets_str.push_back(optarg);
            break;

        case 'R':
            bed_targets_file = optarg;
            break;

        case 'E':
            path_dag = true;
            break;

        case 'P':
            path_name = optarg;
            position_in = true;
            break;

        case 'W':
            save_to_prefix = optarg;
            break;

        case 'c':
            context_size = parse<int>(optarg);
            break;

        case 'L':
            use_length = true;
            break;

        case 'n':
            node_ids.push_back(parse<int>(optarg));
            break;

        case 'N':
            node_list_file = optarg;
            break;

        case 'e':
            end_id = parse<int>(optarg);
            break;

        case 's':
            start_id = parse<int>(optarg);
            break;

        case 'T':
            kmer_table = true;
            break;

        case 'r':
            range = optarg;
            break;
        
        case 'l':
            sorted_gam_name = optarg;
            break;
        
        case 'a':
            get_alignments = true;
            break;

        case 'I':
            list_path_names = true;
            break;

        case 'm':
            get_mappings = true;
            break;

        case 'o':
            aln_on_id_range = optarg;
            break;

        case 'D':
            pairwise_distance = true;
            break;

        case 'Q':
            extract_paths = true;
            extract_path_patterns.push_back(optarg);
            break;

        case 'G':
            gam_file = optarg;
            break;

        case 'A':
            to_graph_file = optarg;
            break;

        case 'K':
            subgraph_k = atoi(optarg);
            break;

        case 'H':
            gbwt_name = optarg;
            break;

        case 'h':
        case '?':
            help_find(argv);
            exit(1);
            break;

        default:
            abort ();
        }
    }
    if (optind < argc) {
        cerr << "[vg find] find does not accept positional arguments" << endl;
        return 1;
    }

    if (db_name.empty() && gcsa_in.empty() && xg_name.empty() && sorted_gam_name.empty()) {
        cerr << "[vg find] find requires -d, -g, -x, or -l to know where to find its database" << endl;
        return 1;
    }

    if (context_size > 0 && use_length == true && xg_name.empty()) {
        cerr << "[vg find] error, -L not supported without -x" << endl;
        exit(1);
    }
    
    if (xg_name.empty() && mem_reseed_length) {
        cerr << "error:[vg find] SMEM reseeding requires an XG index. Provide XG index with -x." << endl;
        exit(1);
    }
    
    // process input node list
    if (!node_list_file.empty()) {
        ifstream nli;
        nli.open(node_list_file);
        if (!nli.good()){
            cerr << "[vg find] error, unable to open the node list input file." << endl;
            exit(1);
        }
        string line;
        while (getline(nli, line)){
            for (auto& idstr : split_delims(line, " \t")) {
                node_ids.push_back(parse<nid_t>(idstr.c_str()));
            }
        }
        nli.close();
    }

    // open RocksDB index
    unique_ptr<Index> vindex;
    if (!db_name.empty()) {
        vindex = unique_ptr<Index>(new Index());
        vindex->open_read_only(db_name);
    }

    PathPositionHandleGraph* xindex = nullptr;
    unique_ptr<PathHandleGraph> path_handle_graph;
    bdsg::PathPositionOverlayHelper overlay_helper;
    if (!xg_name.empty()) {
        path_handle_graph = vg::io::VPKG::load_one<PathHandleGraph>(xg_name);
        xindex = overlay_helper.apply(path_handle_graph.get());
    }

    unique_ptr<gbwt::GBWT> gbwt_index;
    if (!gbwt_name.empty()) {
        // We are tracing haplotypes, and we want to use the GBWT instead of the old gPBWT.
        // Load the GBWT from its container
        gbwt_index = vg::io::VPKG::load_one<gbwt::GBWT>(gbwt_name.c_str());
        if (gbwt_index.get() == nullptr) {
            // Complain if we couldn't.
            cerr << "error:[vg find] unable to load gbwt index file" << endl;
            return 1;
        }
    }
    
    unique_ptr<GAMIndex> gam_index;
    unique_ptr<vg::io::ProtobufIterator<Alignment>> gam_cursor;
    if (!sorted_gam_name.empty()) {
        // Load the GAM index
        gam_index = unique_ptr<GAMIndex>(new GAMIndex());
        get_input_file(sorted_gam_name + ".gai", [&](istream& in) {
            // We get it form the appropriate .gai, which must exist
            gam_index->load(in); 
        });
    }

    if (get_alignments) {
        // Dump all the alignments
        if (vindex.get() != nullptr) {
            // Dump from RocksDB
        
            vector<Alignment> output_buf;
            auto lambda = [&output_buf](const Alignment& aln) {
                output_buf.push_back(aln);
                vg::io::write_buffered(cout, output_buf, 100);
            };
            vindex->for_each_alignment(lambda);
            vg::io::write_buffered(cout, output_buf, 0);
        } else if (gam_index.get() != nullptr) {
            // Dump from sorted GAM
            // TODO: This is basically a noop.
            get_input_file(sorted_gam_name, [&](istream& in) {
                // Stream the alignments in and then stream them back out.
                vg::io::for_each<Alignment>(in, vg::io::emit_to<Alignment>(cout));
            });
        } else {
            cerr << "error [vg find]: Cannot find alignments without a RocksDB index or a sorted GAM" << endl;
            exit(1);
        }
    }

    if (!aln_on_id_range.empty()) {
        // Parse the range
        vector<string> parts = split_delims(aln_on_id_range, ":");
        if (parts.size() == 1) {
            convert(parts.front(), start_id);
            end_id = start_id;
        } else {
            convert(parts.front(), start_id);
            convert(parts.back(), end_id);
        }
        
        if (vindex.get() != nullptr) {
            // Find in RocksDB
            
            // We need a set of all the IDs.
            vector<vg::id_t> ids;
            for (auto i = start_id; i <= end_id; ++i) {
                ids.push_back(i);
            }
            
            vector<Alignment> output_buf;
            auto lambda = [&output_buf](const Alignment& aln) {
                output_buf.push_back(aln);
                vg::io::write_buffered(cout, output_buf, 100);
            };
            vindex->for_alignment_to_nodes(ids, lambda);
            vg::io::write_buffered(cout, output_buf, 0);
        } else if (gam_index.get() != nullptr) {
            // Find in sorted GAM
            
            get_input_file(sorted_gam_name, [&](istream& in) {
                // Make a cursor for input
                // TODO: Refactor so we can put everything with the GAM index inside one get_input_file call to deduplicate code
                vg::io::ProtobufIterator<Alignment> cursor(in);
                
                // Find the alignments and dump them to cout
                gam_index->find(cursor, start_id, end_id, vg::io::emit_to<Alignment>(cout));
            });
            
        } else {
            cerr << "error [vg find]: Cannot find alignments on range without a RocksDB index or a sorted GAM" << endl;
            exit(1);
        }
    }

    if (!to_graph_file.empty()) {
        // Find alignments touching a graph
        
        // Load up the graph
        ifstream tgi(to_graph_file);
        unique_ptr<VG> graph = unique_ptr<VG>(new VG(tgi));
        
        if (vindex.get() != nullptr) {
            // Collet the IDs in a vector
            vector<vg::id_t> ids;
            graph->for_each_node([&](Node* n) { ids.push_back(n->id()); });
            
            // Throw out the graph
            graph.reset();
        
            // Find in RocksDB
            vector<Alignment> output_buf;
            auto lambda = [&output_buf](const Alignment& aln) {
                output_buf.push_back(aln);
                vg::io::write_buffered(cout, output_buf, 100);
            };
            vindex->for_alignment_to_nodes(ids, lambda);
            vg::io::write_buffered(cout, output_buf, 0);
            
        }  else if (gam_index.get() != nullptr) {
            // Find in sorted GAM
            
            // Get the ID ranges from the graph
            auto ranges = vg::algorithms::sorted_id_ranges(graph.get());
            // Throw out the graph
            graph.reset();
            
            get_input_file(sorted_gam_name, [&](istream& in) {
                // Make a cursor for input
                // TODO: Refactor so we can put everything with the GAM index inside one get_input_file call to deduplicate code
                vg::io::ProtobufIterator<Alignment> cursor(in);
            
                // Find the alignments and send them to cout
                gam_index->find(cursor, ranges, vg::io::emit_to<Alignment>(cout)); 
            });
            
        } else {
            cerr << "error [vg find]: Cannot find alignments on graph without a RocksDB index or a sorted GAM" << endl;
            exit(1);
        }
        
        
    }

    if (!xg_name.empty()) {
        if (!node_ids.empty() && path_name.empty() && !pairwise_distance) {
            VG graph;
            for (auto node_id : node_ids) {
                graph.create_handle(xindex->get_sequence(xindex->get_handle(node_id)), node_id);
            }
            if (context_size > 0) {
                if (use_length) {
                    algorithms::expand_subgraph_by_length(*xindex, graph, context_size);
                } else {
                    algorithms::expand_subgraph_by_steps(*xindex, graph, context_size);
                }
            } else {
                algorithms::add_connecting_edges_to_subgraph(*xindex, graph);
            }
            algorithms::add_subpaths_to_subgraph(*xindex, graph);

            graph.remove_orphan_edges();
            
            // Order the mappings by rank. TODO: how do we handle breaks between
            // different sections of a path with a single name?
            graph.paths.sort_by_mapping_rank();
            
            // return it
            graph.serialize_to_ostream(cout);
            // TODO: We're serializing graphs all with their own redundant EOF markers if we use multiple functions simultaneously.
        } else if (end_id != 0) {
            xindex->follow_edges(xindex->get_handle(end_id), false, [&](handle_t next) {
                    edge_t e = xindex->edge_handle(xindex->get_handle(end_id, false), next);
                    cout << (xindex->get_is_reverse(e.first) ? -1 : 1) * xindex->get_id(e.first) << "\t"
                         << (xindex->get_is_reverse(e.second) ? -1 : 1) * xindex->get_id(e.second) << endl;
                });
        } else if (start_id != 0) {
            xindex->follow_edges(xindex->get_handle(start_id), true, [&](handle_t next) {
                    edge_t e = xindex->edge_handle(xindex->get_handle(start_id, true), next);
                    cout << (xindex->get_is_reverse(e.first) ? -1 : 1) * xindex->get_id(e.first) << "\t"
                         << (xindex->get_is_reverse(e.second) ? -1 : 1) * xindex->get_id(e.second) << endl;
                });
        }
        if (!node_ids.empty() && !path_name.empty() && !pairwise_distance && position_in) {
            // Go get the positions of these nodes in this path
            if (xindex->has_path(path_name) == false) {
                // This path doesn't exist, and we'll get a segfault or worse if
                // we go look for positions in it.
                cerr << "[vg find] error, path \"" << path_name << "\" not found in index" << endl;
                exit(1);
            }
            
            // Note: this isn't at all consistent with -P option with rocksdb, which couts a range
            // and then mapping, but need this info right now for scripts/chunked_call
            path_handle_t path_handle = xindex->get_path_handle(path_name);
            for (auto node_id : node_ids) {
                cout << node_id;
                assert(position_in);
                xindex->for_each_step_on_handle(xindex->get_handle(node_id), [&](step_handle_t step_handle) {
                        if (xindex->get_path_handle_of_step(step_handle) == path_handle) {
                            cout << "\t" << xindex->get_position_of_step(step_handle);
                        }
                    });
                cout << endl;
            }
        }
        if (pairwise_distance) {
            if (node_ids.size() != 2) {
                cerr << "[vg find] error, exactly 2 nodes (-n) required with -D" << endl;
                exit(1);
            }
            cout << algorithms::min_approx_path_distance(dynamic_cast<PathPositionHandleGraph*>(&*xindex), make_pos_t(node_ids[0], false, 0), make_pos_t(node_ids[1], false, 0), 1000) << endl;
            return 0;
        }
        if (list_path_names) {
            xindex->for_each_path_handle([&](path_handle_t path_handle) {
                    cout << xindex->get_path_name(path_handle) << endl;
                });
        }
        // handle targets from BED
        if (!bed_targets_file.empty()) {
            parse_bed_regions(bed_targets_file, targets);
        }
        // those given on the command line
        for (auto& target : targets_str) {
            Region region;
            parse_region(target, region);
            targets.push_back(region);
        }
        if (!targets.empty()) {
            VG graph;
            auto prep_graph = [&](void) {
                if (context_size > 0) {
                    if (use_length) {
                        algorithms::expand_subgraph_by_length(*xindex, graph, context_size);
                    } else {
                        algorithms::expand_subgraph_by_steps(*xindex, graph, context_size);
                    }
                } else {
                    algorithms::add_connecting_edges_to_subgraph(*xindex, graph);
                }
                algorithms::add_subpaths_to_subgraph(*xindex, graph);
                graph.remove_orphan_edges();
                // Order the mappings by rank. TODO: how do we handle breaks between
                // different sections of a path with a single name?
                graph.paths.sort_by_mapping_rank();
            };
            for (auto& target : targets) {
                // Grab each target region
                if(xindex->has_path(target.seq) == false) { 
                    // Passing a nonexistent path to get_path_range produces Undefined Behavior
                    cerr << "[vg find] error, path " << target.seq << " not found in index" << endl;
                    exit(1);
                }
                path_handle_t path_handle = xindex->get_path_handle(target.seq);
                // no coordinates given, we do whole thing (0,-1)
                if (target.start < 0 && target.end < 0) {
                    target.start = 0;
                }
                algorithms::extract_path_range(*xindex, path_handle, target.start, target.end, graph);
                if (path_dag) {
                    // find the start and end node of this
                    // and fill things in
                    nid_t id_start = std::numeric_limits<nid_t>::max();
                    nid_t id_end = 1;
                    graph.for_each_handle([&](handle_t handle) {
                            nid_t id = graph.get_id(handle);
                            id_start = std::min(id_start, id);
                            id_end = std::max(id_end, id);
                        });
                    algorithms::extract_id_range(*xindex, id_start, id_end, graph);
                }
                if (!save_to_prefix.empty()) {
                    prep_graph();
                    // write to our save_to file
                    stringstream s;
                    s << save_to_prefix << target.seq;
                    if (target.end >= 0) s << ":" << target.start << ":" << target.end;
                    s << ".vg";
                    ofstream out(s.str().c_str());
                    graph.serialize_to_ostream(out);
                    out.close();
                    // reset our graph
                    VG empty;
                    graph = empty;
                }
                if (subgraph_k) {
                    prep_graph(); // don't forget to prep the graph, or the kmer set will be wrong[
                    // enumerate the kmers, calculating including their start positions relative to the reference
                    // and write to stdout?
                    bool use_gbwt = false;
                    if (!gbwt_name.empty()) {
                        use_gbwt = true;
                    }
                    algorithms::for_each_walk(
                        graph, subgraph_k, 0,
                        [&](const algorithms::walk_t& walk) {
                            // get the reference-relative position
                            string start_str, end_str;
                            for (auto& p : algorithms::nearest_offsets_in_paths(xindex, walk.begin, subgraph_k*2)) {
                                const uint64_t& start_p = p.second.front().first;
                                const bool& start_rev = p.second.front().second;
                                if (p.first == path_handle && (!start_rev && start_p >= target.start || start_rev && start_p <= target.end)) {
                                    start_str = target.seq + ":" + std::to_string(start_p) + (p.second.front().second ? "-" : "+");
                                }
                            }
                            for (auto& p : algorithms::nearest_offsets_in_paths(xindex, walk.end, subgraph_k*2)) {
                                const uint64_t& end_p = p.second.front().first;
                                const bool& end_rev = p.second.front().second;
                                if (p.first == path_handle && (!end_rev && end_p <= target.end || end_rev && end_p >= target.start)) {
                                    end_str = target.seq + ":" + std::to_string(end_p) + (p.second.front().second ? "-" : "+");
                                }
                            }
                            if (!start_str.empty() && !end_str.empty()) {
                                stringstream ss;
                                ss << target.seq << ":" << target.start << "-" << target.end << "\t"
                                   << walk.seq << "\t" << start_str << "\t" << end_str << "\t";
                                uint64_t on_path = 0;
                                for (auto& h : walk.path) {
                                    xindex->for_each_step_on_handle(xindex->get_handle(graph.get_id(h), graph.get_is_reverse(h)),
                                                                    [&](const step_handle_t& step) {
                                                                        if (xindex->get_path_handle_of_step(step) == path_handle) {
                                                                            ++on_path;
                                                                        }
                                                                    });
                                }
                                // get haplotype frequency
                                if (use_gbwt) {
                                    ss << walk_haplotype_frequency(graph, *gbwt_index, walk) << "\t";
                                } else {
                                    ss << 0 << "\t";
                                }
                                if (on_path == walk.path.size()) {
                                    ss << "ref" << "\t";
                                } else {
                                    ss << "non.ref" << "\t";
                                }
                                for (auto& h : walk.path) {
                                    ss << graph.get_id(h) << (graph.get_is_reverse(h)?"-":"+") << ",";
                                }
                                // write our record
#pragma omp critical (cout)
                                cout << ss.str() << std::endl;
                            }
                        });
                }
            }
            if (save_to_prefix.empty() && !subgraph_k) {
                prep_graph();
                graph.serialize_to_ostream(cout);
            }
        }
        if (!range.empty()) {
            VG graph;
            nid_t id_start=0, id_end=0;
            vector<string> parts = split_delims(range, ":");
            if (parts.size() == 1) {
                cerr << "[vg find] error, format of range must be \"N:M\" where start id is N and end id is M, got " << range << endl;
                exit(1);
            }
            convert(parts.front(), id_start);
            convert(parts.back(), id_end);
            if (!use_length) {
                algorithms::extract_id_range(*xindex, id_start, id_end, graph);
            } else {
                // treat id_end as length instead.
                size_t length = 0;
                nid_t found_id_end = id_start;
                for (nid_t cur_id = id_start; length < id_end; ++cur_id) {
                    if (!xindex->has_node(cur_id)) {
                        break;
                    }
                    length += xindex->get_length(xindex->get_handle(cur_id));
                    found_id_end = cur_id;
                }
                algorithms::extract_id_range(*xindex, id_start, found_id_end, graph);
            }
            if (context_size > 0) {
                if (use_length) {
                    algorithms::expand_subgraph_by_length(*xindex, graph, context_size);
                } else {
                    algorithms::expand_subgraph_by_steps(*xindex, graph, context_size);
                }
            } else {
                algorithms::add_connecting_edges_to_subgraph(*xindex, graph);
            }
            algorithms::add_subpaths_to_subgraph(*xindex, graph);

            graph.remove_orphan_edges();
            graph.serialize_to_ostream(cout);
        }
        if (extract_paths) {
            for (auto& pattern : extract_path_patterns) {
            
                // We want to write uncompressed protobuf Graph objects containing our paths.
                vg::io::ProtobufEmitter<Graph> out(cout, false);
            
                xindex->for_each_path_handle([&](path_handle_t path_handle) {
                        string path_name = xindex->get_path_name(path_handle);
                        if (pattern.length() <= path_name.length() && path_name.compare(0, pattern.length(), pattern) == 0) {
                            // We need a Graph for serialization purposes.
                            Graph g;
                            *g.add_path() = path_from_path_handle(*xindex, path_handle);
                            out.write(std::move(g));
                        }
                    });
            }
        }
        if (!gam_file.empty()) {
            set<vg::id_t> nodes;
            function<void(Alignment&)> lambda = [&nodes](Alignment& aln) {
                // accumulate nodes matched by the path
                auto& path = aln.path();
                if (path.mapping_size() == 1 && !path.mapping(0).has_position() &&
                    path.mapping(0).edit_size() == 1 && edit_is_insertion(path.mapping(0).edit(0))) {
                    // This read is a (presumably full length) insert to no
                    // position. The aligner (used to?) generate these for some
                    // unmapped reads. We should skip it.
                    return;
                }
                for (int i = 0; i < path.mapping_size(); ++i) {
                    nodes.insert(path.mapping(i).position().node_id());
                }
            };
            if (gam_file == "-") {
                vg::io::for_each(std::cin, lambda);
            } else {
                ifstream in;
                in.open(gam_file.c_str());
                if(!in.is_open()) {
                    cerr << "[vg find] error: could not open alignments file " << gam_file << endl;
                    exit(1);
                }
                vg::io::for_each(in, lambda);
            }
            // now we have the nodes to get
            VG graph;
            for (auto& node : nodes) {
                handle_t node_handle = xindex->get_handle(node);
                graph.create_handle(xindex->get_sequence(node_handle), xindex->get_id(node_handle));
            }
            algorithms::expand_subgraph_by_steps(*xindex, graph, max(1, context_size)); // get connected edges
            algorithms::add_connecting_edges_to_subgraph(*xindex, graph);
            graph.serialize_to_ostream(cout);
        }
    } else if (!db_name.empty()) {
        if (!node_ids.empty() && path_name.empty()) {
            // get the context of the node
            vector<VG> graphs;
            for (auto node_id : node_ids) {
                VG g;
                vindex->get_context(node_id, g);
                if (context_size > 0) {
                    vindex->expand_context(g, context_size);
                }
                graphs.push_back(g);
            }
            VG result_graph;
            for (auto& graph : graphs) {
                // Allow duplicate nodes and edges (from e.g. multiple -n options); silently collapse them.
                result_graph.extend(graph);
            }
            result_graph.remove_orphan_edges();
            // return it
            result_graph.serialize_to_ostream(cout);
        } else if (end_id != 0) {
            vector<Edge> edges;
            vindex->get_edges_on_end(end_id, edges);
            for (vector<Edge>::iterator e = edges.begin(); e != edges.end(); ++e) {
                cout << (e->from_start() ? -1 : 1) * e->from() << "\t" <<  (e->to_end() ? -1 : 1) * e->to() << endl;
            }
        } else if (start_id != 0) {
            vector<Edge> edges;
            vindex->get_edges_on_start(start_id, edges);
            for (vector<Edge>::iterator e = edges.begin(); e != edges.end(); ++e) {
                cout << (e->from_start() ? -1 : 1) * e->from() << "\t" <<  (e->to_end() ? -1 : 1) * e->to() << endl;
            }
        }
        if (!node_ids.empty() && !path_name.empty()) {
            int64_t path_id = vindex->get_path_id(path_name);
            for (auto node_id : node_ids) {
                list<pair<nid_t, bool>> path_prev, path_next;
                int64_t prev_pos=0, next_pos=0;
                bool prev_backward, next_backward;
                if (vindex->get_node_path_relative_position(node_id, false, path_id,
                            path_prev, prev_pos, prev_backward,
                            path_next, next_pos, next_backward)) {

                    // Negate IDs for backward nodes
                    cout << node_id << "\t" << path_prev.front().first * (path_prev.front().second ? -1 : 1) << "\t" << prev_pos
                        << "\t" << path_next.back().first * (path_next.back().second ? -1 : 1) << "\t" << next_pos << "\t";

                    Mapping m = vindex->path_relative_mapping(node_id, false, path_id,
                            path_prev, prev_pos, prev_backward,
                            path_next, next_pos, next_backward);
                    cout << pb2json(m) << endl;
                }
            }
        }
        if (!range.empty()) {
            VG graph;
            nid_t id_start=0, id_end=0;
            vector<string> parts = split_delims(range, ":");
            if (parts.size() == 1) {
                cerr << "[vg find] error, format of range must be \"N:M\" where start id is N and end id is M, got " << range << endl;
                exit(1);
            }
            convert(parts.front(), id_start);
            convert(parts.back(), id_end);
            vindex->get_range(id_start, id_end, graph);
            if (context_size > 0) {
                vindex->expand_context(graph, context_size);
            }
            graph.remove_orphan_edges();
            graph.serialize_to_ostream(cout);
        }
    }

    // todo cleanup if/else logic to allow only one function

    if (!sequence.empty()) {
        if (gcsa_in.empty()) {
            if (get_mems) {
                cerr << "error:[vg find] a GCSA index must be passed to get MEMs" << endl;
                return 1;
            }
            set<int> kmer_sizes = vindex->stored_kmer_sizes();
            if (kmer_sizes.empty()) {
                cerr << "error:[vg find] index does not include kmers, add with vg index -k" << endl;
                return 1;
            }
            if (kmer_size == 0) {
                kmer_size = *kmer_sizes.begin();
            }
            for (int i = 0; i <= sequence.size()-kmer_size; i+=kmer_stride) {
                kmers.push_back(sequence.substr(i,kmer_size));
            }
        } else {
            // let's use the GCSA index

            // Configure GCSA2 verbosity so it doesn't spit out loads of extra info
            gcsa::Verbosity::set(gcsa::Verbosity::SILENT);
            
            // Configure its temp directory to the system temp directory
            gcsa::TempFile::setDirectory(temp_file::get_dir());

            // Open it
            auto gcsa_index = vg::io::VPKG::load_one<gcsa::GCSA>(gcsa_in);
            // default LCP is the gcsa base name +.lcp
            auto lcp_index = vg::io::VPKG::load_one<gcsa::LCPArray>(gcsa_in + ".lcp");
            
            //range_type find(const char* pattern, size_type length) const;
            //void locate(size_type path, std::vector<node_type>& results, bool append = false, bool sort = true) const;
            //locate(i, results);
            if (!get_mems) {
                auto paths = gcsa_index->find(sequence.c_str(), sequence.length());
                //cerr << paths.first << " - " << paths.second << endl;
                for (gcsa::size_type i = paths.first; i <= paths.second; ++i) {
                    std::vector<gcsa::node_type> ids;
                    gcsa_index->locate(i, ids);
                    for (auto id : ids) {
                        cout << gcsa::Node::decode(id) << endl;
                    }
                }
            } else {
                // for mems we need to load up the gcsa and lcp structures into the mapper
                Mapper mapper(xindex, gcsa_index.get(), lcp_index.get());
                mapper.fast_reseed = use_fast_reseed;
                // get the mems
                double lcp_avg, fraction_filtered;
                auto mems = mapper.find_mems_deep(sequence.begin(), sequence.end(), lcp_avg, fraction_filtered, max_mem_length, min_mem_length, mem_reseed_length);

                // dump them to stdout
                cout << mems_to_json(mems) << endl;

            }
        }
    }

    if (!kmers.empty()) {
        if (count_kmers) {
            for (auto& kmer : kmers) {
                cout << kmer << "\t" << vindex->approx_size_of_kmer_matches(kmer) << endl;
            }
        } else if (kmer_table) {
            for (auto& kmer : kmers) {
                map<string, vector<pair<nid_t, int32_t> > > positions;
                vindex->get_kmer_positions(kmer, positions);
                for (auto& k : positions) {
                    for (auto& p : k.second) {
                        cout << k.first << "\t" << p.first << "\t" << p.second << endl;
                    }
                }
            }
        } else {
            vector<VG> graphs;
            for (auto& kmer : kmers) {
                VG g;
                vindex->get_kmer_subgraph(kmer, g);
                if (context_size > 0) {
                    vindex->expand_context(g, context_size);
                }
                graphs.push_back(g);
            }

            VG result_graph;
            for (auto& graph : graphs) {
                // Allow duplicate nodes and edges (from multiple kmers); silently collapse them.
                result_graph.extend(graph);
            }
            result_graph.remove_orphan_edges();
            result_graph.serialize_to_ostream(cout);
        }
    }
    
    return 0;

}

static Subcommand vg_msga("find", "use an index to find nodes, edges, kmers, paths, or positions", TOOLKIT, main_find);
