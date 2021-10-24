# VG SNP-Aware

VG SNP-Aware introduces an optimised alignment process for SNPs,  with the idea to  exploit  only  exact  alignments,  using  a  depth-limited  search  like.   The  process  is composed by two distinct phases: searching for initial node and mapping from it. Both phases are based on the characteristics of the variation graph in case of SNPs.

vg represents the two nucleotides of a biallelic SNP as two possible nodes on graph and for each of them provides an edge from and to it.  This approach allows to have multiple paths which admits all possible sequences on graph, including the reference base or the alternative base of SNPs.  The naming convention applied to the path requires alternative path having a name starting with ”alt”,  this allows to distinguish it from the reference paths. It is important to note that each node belongs to one or more paths.

![Variation graph](https://raw.githubusercontent.com/monsmau/vg/vg_snp_aware/doc/figures/variationgraph_with_SNP_ID_SEQ_PATH.png)

Algorithms for initial node search and for mapping have been included in the vg map command. Thus, VG SNP-Aware is a custom reimplementation of the map command of vg. 

## Installation

### Download Releases

The easiest way to get vg is to download one of our release builds for Linux. We have a 6-week release cadence, so our builds are never too far out of date.

**[![Download Button](doc/figures/download-linux.png)](https://github.com/vgteam/vg/releases/latest)**  
**[Download the latest vg release for Linux](https://github.com/vgteam/vg/releases/latest)**


### Building on Linux

If you don't want to or can't use a pre-built release of vg, or if you want to become a vg developer, you can build it from source instead.

First, obtain the repo and its submodules:

    git clone --recursive https://github.com/vgteam/vg.git
    cd vg
    
Then, install VG's dependencies. You'll need the protobuf and jansson development libraries installed, and to run the tests you will need `jq`, `bc` and `rs`. On Ubuntu, you should be able to do:

    make get-deps
    
On other distros, you will need to perform the equivalent of:

    sudo apt-get install build-essential git cmake pkg-config libncurses-dev libbz2-dev  \
                         protobuf-compiler libprotoc-dev libprotobuf-dev libjansson-dev \
                         automake libtool jq bc rs curl unzip redland-utils \
                         librdf-dev bison flex gawk lzma-dev liblzma-dev liblz4-dev \
                         libffi-dev libcairo-dev libboost-all-dev
                         
Note that **Ubuntu 16.04** does not ship a sufficiently new Protobuf; vg requires **Protobuf 3** which will have to be manually installed.

At present, you will need GCC version 4.9 or greater, with support for C++14, to compile vg. (Check your version with `gcc --version`.)

Other libraries may be required. Please report any build difficulties.

Note that a 64-bit OS is required. Ubuntu 18.04 should work. You will also need a CPU that supports SSE 4.2 to run VG; you can check this with `cat /proc/cpuinfo | grep sse4_2`.

When you are ready, build with `. ./source_me.sh && make`, and run with `./bin/vg`.

You can also produce a static binary with `make static`, assuming you have static versions of all the dependencies installed on your system.




## Usage



# Getting help
If you encounter bugs or have further questions or requests, you can raise an issue at the issue page. You can also contact Maurilio Monsù at maurilio.monsu@studenti.unipd.it

# Citation
 M. Monsù, M. Comin, "Fast Alignment of Reads to a Variation Graph with Application to SNP Detection", under submission.


