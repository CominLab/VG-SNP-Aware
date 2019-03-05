/** \file minimizer_main.cpp
 *
 * Defines the "vg minimizer" subcommand, which builds the experimental
 * minimizer index.
 *
 * The index contains the lexicographically smallest kmer in a window of w
 * successive kmers. If the kmer contains characters other than A, C, G, and
 * T, it will not be indexed.
 *
 * By default, the index contains all minimizers in the graph. Option
 * --max-occs can be used to specify the maximum number of occurrences for
 * a kmer. Kmers more frequent than that will be removed from the index.
 *
 * The index contains either all minimizers or haplotype-consistent minimizers
 * (option --gbwt-name). Indexing all minimizers from complex graph regions
 * can take a long time (e.g. 65 hours vs 30 minutes for 1000GP), because many
 * windows have the same minimizer. As the total number of minimizers is
 * manageable (e.g. 2.1 billion vs. 1.4 billion for 1000GP), it should be
 * possible to develop a better algorithm for finding the minimizers.
 *
 * A quick idea:
 * - For each node v, extract the subgraph for the windows starting in v.
 * - Extract all k'-mers from the subgraph and use them to determine where the
 *   minimizers can start.
 */

#include "../gbwt_helper.hpp"
#include "../minimizer.hpp"
#include "subcommand.hpp"

#include "../stream/vpkg.hpp"
#include "../stream/stream.hpp"

#include <gcsa/gcsa.h>
#include <gcsa/lcp.h>

#include <iostream>
#include <vector>

#include <getopt.h>
#include <omp.h>

using namespace vg;
using namespace vg::subcommand;


void help_minimizer(char** argv) {
    std::cerr << "usage: " << argv[0] << " minimizer [options] graph.xg" << std::endl;
    std::cerr << "Builds a minimizer index of the graph in the XG index." << std::endl;
    std::cerr << "    -k, --kmer-length N    length of the kmers in the index (default: " << MinimizerIndex::KMER_LENGTH << ")" << std::endl;
    std::cerr << "    -w, --window-length N  index the smallest kmer in a window of N kmers (default: " << MinimizerIndex::WINDOW_LENGTH << ")" << std::endl;
    std::cerr << "    -m, --max-occs N       do not index minimizers with more than N occurrences" << std::endl;
    std::cerr << "    -i, --index-name X     store the index to file X (required)" << std::endl;
    std::cerr << "    -l, --load-index X     load the index from file X and insert the new kmers into it" << std::endl;
    std::cerr << "                           (overrides --kmer-length, --window-length, and --max-occs)" << std::endl;
    std::cerr << "    -g, --gbwt-name X      index only haplotype-consistent kmers using the GBWT index in file X" << std::endl;
    std::cerr << "    -p, --progress         show progress information" << std::endl;
    std::cerr << "    -t, --threads N        use N threads for index construction (default: " << omp_get_max_threads() << ")" << std::endl;
    std::cerr << "benchmark options:" << std::endl;
    std::cerr << "    -b, --benchmark X      query performance benchmarks with the sequences in file X" << std::endl;
    std::cerr << "    -i, --index-name X     benchmark the minimizer index in file X (required)" << std::endl;
    std::cerr << "    -G, --gcsa-name X      also benchmark the GCSA2 index in file X" << std::endl;
    std::cerr << "    -L, --locate           locate the minimizer/MEM occurrences" << std::endl;
    std::cerr << "    -m, --max-occs N       do not locate minimizers with more than N occurrences" << std::endl;
}

int query_benchmarks(const std::string& index_name, const std::string& reads_name, const std::string& gcsa_name, bool locate, size_t max_occs, bool progress);

int main_minimizer(int argc, char** argv) {

    if (argc == 2) {
        help_minimizer(argv);
        return 1;
    }

    // Command-line options.
    size_t kmer_length = MinimizerIndex::KMER_LENGTH;
    size_t window_length = MinimizerIndex::WINDOW_LENGTH;
    size_t max_occs = MinimizerIndex::MAX_OCCS;
    std::string index_name, load_index, gbwt_name, xg_name, reads_name, gcsa_name;
    bool progress = false, locate = false;
    int threads = omp_get_max_threads();

    int c;
    optind = 2; // force optind past command positional argument
    while (true) {
        static struct option long_options[] =
        {
            { "kmer-length", required_argument, 0, 'k' },
            { "window-length", required_argument, 0, 'w' },
            { "max-occs", required_argument, 0, 'm' },
            { "index-name", required_argument, 0, 'i' },
            { "load-index", required_argument, 0, 'l' },
            { "gbwt-name", required_argument, 0, 'g' },
            { "progress", no_argument, 0, 'p' },
            { "threads", required_argument, 0, 't' },
            { "benchmark", required_argument, 0, 'b' },
            { "gcsa-name", required_argument, 0, 'G' },
            { "locate", no_argument, 0, 'L' },
            { 0, 0, 0, 0 }
        };

        int option_index = 0;
        c = getopt_long(argc, argv, "k:w:m:i:l:g:pt:hb:G:L", long_options, &option_index);
        if (c == -1) { break; } // End of options.

        switch (c)
        {
        case 'k':
            kmer_length = parse<size_t>(optarg);
            break;
        case 'w':
            window_length = parse<size_t>(optarg);
            break;
        case 'm':
            max_occs = parse<size_t>(optarg);
            break;
        case 'i':
            index_name = optarg;
            break;
        case 'l':
            load_index = optarg;
            break;
        case 'g':
            gbwt_name = optarg;
            break;
        case 'p':
            progress = true;
            break;
        case 't':
            threads = parse<int>(optarg);
            threads = std::min(threads, omp_get_max_threads());
            threads = std::max(threads, 1);
            omp_set_num_threads(threads);
            break;
        case 'b':
            reads_name = optarg;
            break;
        case 'G':
            gcsa_name = optarg;
            break;
        case 'L':
            locate = true;
            break;

        case 'h':
        case '?':
            help_minimizer(argv);
            return 1;
        default:
            std::abort();
        }
    }
    if (index_name.empty()) {
        std::cerr << "[vg minimizer]: option --index-name is required" << std::endl;
        return 1;
    }
    if (!reads_name.empty()) {
        return query_benchmarks(index_name, reads_name, gcsa_name, locate, max_occs, progress);
    }
    if (optind + 1 != argc) {
        help_minimizer(argv);
        return 1;
    }
    xg_name = argv[optind];

    double start = gbwt::readTimer();

    // Input graph.
    if (progress) {
        std::cerr << "Loading XG index " << xg_name << std::endl;
    }
    std::unique_ptr<xg::XG> xg_index;
    xg_index = stream::VPKG::load_one<xg::XG>(xg_name);

    // Minimizer index.
    std::unique_ptr<MinimizerIndex> index(new MinimizerIndex(kmer_length, window_length, max_occs));
    if (!load_index.empty()) {
        if (progress) {
            std::cerr << "Loading the index from " << load_index << std::endl;
        }
        index = stream::VPKG::load_one<MinimizerIndex>(load_index);
    }

    // GBWT index.
    std::unique_ptr<gbwt::GBWT> gbwt_index;
    if (!gbwt_name.empty()) {
        if (progress) {
            std::cerr << "Loading GBWT index " << gbwt_name << std::endl;
        }
        gbwt_index = stream::VPKG::load_one<gbwt::GBWT>(gbwt_name);
    }

    // Build the index.
    if (progress) {
        std::cerr << "Building the index" << std::endl;
    }
    auto lambda = [&index](const std::vector<std::pair<pos_t, size_t>>& traversal, const std::string& seq) {
        std::vector<MinimizerIndex::minimizer_type> minimizers = index->minimizers(seq);
        auto iter = traversal.begin();
        size_t starting_offset = 0;
#pragma omp critical (minimizer_index)
        {
            for (MinimizerIndex::minimizer_type minimizer : minimizers) {
                if (minimizer.first == MinimizerIndex::NO_KEY) {
                    continue;
                }
                // Find the node covering minimizer starting position.
                while (starting_offset + iter->second <= minimizer.second) {
                    starting_offset += iter->second;
                    ++iter;
                }
                pos_t pos = iter->first;
                get_offset(pos) += minimizer.second - starting_offset;
                index->insert(minimizer.first, pos);
            }
        }
    };
    if (gbwt_name.empty()) {
        for_each_window(*xg_index, index->k() + index->w() - 1, lambda, (threads > 1));
    } else {
        for_each_window(*xg_index, *gbwt_index, index->k() + index->w() - 1, lambda, (threads > 1));
    }
    gbwt_index.reset(nullptr);

    // Index statistics.
    if (progress) {
        std::cerr << index->size() << " keys (" << index->unique_keys() << " unique, " << index->frequent_keys() << " too frequent)" << std::endl;
        std::cerr << "Minimizer occurrences: " << index->values() << std::endl;
        std::cerr << "Load factor: " << index->load_factor() << std::endl;
    }

    // Serialize the index.
    if (progress) {
        std::cerr << "Writing the index to " << index_name << std::endl;
    }
    stream::VPKG::save(*index, index_name);

    double seconds = gbwt::readTimer() - start;
    if (progress) {
        std::cerr << "Time usage: " << seconds << " seconds" << std::endl;
        std::cerr << "Memory usage: " << gbwt::inGigabytes(gbwt::memoryUsage()) << " GiB" << std::endl;
    }

    return 0;
}

int query_benchmarks(const std::string& index_name, const std::string& reads_name, const std::string& gcsa_name, bool locate, size_t max_occs, bool progress) {

    double start = gbwt::readTimer();

    // Load the minimizer index.
    if (progress) {
        std::cerr << "Loading the minimizer index from " << index_name << std::endl;
    }
    std::unique_ptr<MinimizerIndex> index(stream::VPKG::load_one<MinimizerIndex>(index_name));

    // Load the GCSA index.
    bool benchmark_gcsa = !(gcsa_name.empty());
    std::unique_ptr<gcsa::GCSA> gcsa_index;
    std::unique_ptr<gcsa::LCPArray> lcp_index;
    if (benchmark_gcsa) {
        if (progress) {
            std::cerr << "Loading the GCSA index from " << gcsa_name << std::endl;
        }
        gcsa_index = stream::VPKG::load_one<gcsa::GCSA>(gcsa_name);
        std::string lcp_name = gcsa_name + gcsa::LCPArray::EXTENSION;
        lcp_index = stream::VPKG::load_one<gcsa::LCPArray>(lcp_name);
    }

    // Load the reads.
    std::vector<std::string> reads;
    if (progress) {
        std::cerr << "Loading the reads from " << reads_name << std::endl;
    }
    size_t total_size = gcsa::readRows(reads_name, reads, true);
    if (progress) {
        std::cerr << reads.size() << " reads of total length " << total_size << std::endl;
    }
    if (reads.empty()) {
        return 0;
    }

    size_t threads = omp_get_max_threads();
    if (progress) {
        std::cerr << "Query threads: " << threads << std::endl;
        std::cerr << std::endl;
    }

    // Minimizers.
    {
        double phase_start = gbwt::readTimer();

        std::vector<size_t> min_counts(threads, 0);
        std::vector<size_t> occ_counts(threads, 0);
#pragma omp parallel for schedule(static)
        for (size_t i = 0; i < reads.size(); i++) {
            size_t thread = omp_get_thread_num();
            std::vector<MinimizerIndex::minimizer_type> result = index->minimizers(reads[i]);
            min_counts[thread] += result.size();
            if (locate) {
                for (auto minimizer : result) {
                    if (index->count(minimizer.first) <= max_occs) {
                        std::vector<pos_t> result = index->find(minimizer.first);
                        if (result.size() != 1 || !vg::is_empty(result.front())) {
                            occ_counts[thread] += result.size();
                        }
                    }
                }
            } else {
                for (auto minimizer : result) {
                    occ_counts[thread] += index->count(minimizer.first);
                }
            }
        }
        size_t min_count = 0, occ_count = 0;
        for (size_t i = 0; i < threads; i++) {
            min_count += min_counts[i];
            occ_count += occ_counts[i];
        }

        double phase_seconds = gbwt::readTimer() - phase_start;
        std::string query_type = (locate ? "locate" : "count");
        std::cerr << "Minimizers (" << query_type << "): " << phase_seconds << " seconds (" << (reads.size() / phase_seconds) << " reads/second)" << std::endl;
        std::cerr << min_count << " minimizers with " << occ_count << " occurrences" << std::endl;
        std::cerr << std::endl;
    }

    // Count MEMs.
    if (benchmark_gcsa) {
        double phase_start = gbwt::readTimer();

        std::vector<size_t> mem_counts(threads, 0);
        std::vector<size_t> mem_lengths(threads, 0);
#pragma omp parallel for schedule(static)
        for (size_t i = 0; i < reads.size(); i++) {
            size_t thread = omp_get_thread_num();
            const std::string& read = reads[i];
            auto iter = read.rbegin();
            gcsa::range_type full(0, gcsa_index->size() - 1);
            gcsa::range_type curr = full;
            std::vector<gcsa::node_type> occurrences;
            while (iter != read.rend()) {
                gcsa::range_type prev = curr;
                curr = gcsa_index->LF(curr, gcsa_index->alpha.char2comp[*iter]);
                if (gcsa::Range::empty(curr)) {
                    if (prev != full) {
                        mem_counts[thread]++;
                        if (locate) {
                            gcsa_index->locate(prev, occurrences);
                            mem_lengths[thread] += occurrences.size();
                        } else {
                            mem_lengths[thread] += gcsa::Range::length(prev);
                        }
                        gcsa::STNode parent = lcp_index->parent(prev);
                        curr = parent.range();
                    } else {
                        curr = full;
                    }
                } else if (iter + 1 == read.rend()) {
                    if (prev != full) {
                        mem_counts[thread]++;
                        if (locate) {
                            gcsa_index->locate(prev, occurrences);
                            mem_lengths[thread] += occurrences.size();
                        } else {
                            mem_lengths[thread] += gcsa::Range::length(prev);
                        }
                    }
                }
                ++iter;
            }
        }
        size_t mem_count = 0, mem_length = 0;
        for (size_t i = 0; i < threads; i++) {
            mem_count += mem_counts[i];
            mem_length += mem_lengths[i];
        }

        double phase_seconds = gbwt::readTimer() - phase_start;
        std::string query_type = (locate ? "locate" : "count");
        std::cerr << "MEMs (" << query_type << "): " << phase_seconds << " seconds (" << (reads.size() / phase_seconds) << " reads/second)" << std::endl;
        std::cerr << mem_count << " MEMs with " << mem_length << " occurrences" << std::endl;
        std::cerr << std::endl;
    }


    // Locate minimizers
    // Locate MEMs

    double seconds = gbwt::readTimer() - start;
    std::cerr << "Benchmarks completed in " << seconds << " seconds" << std::endl;
    std::cerr << "Memory usage: " << gbwt::inGigabytes(gbwt::memoryUsage()) << " GiB" << std::endl;

    return 0;
}

// FIXME change from DEVELOPMENT to TOOLKIT or PIPELINE later
// Register subcommand
static Subcommand vg_minimizer("minimizer", "build a minimizer index", DEVELOPMENT, main_minimizer);

