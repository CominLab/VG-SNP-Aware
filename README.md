# VG SNP-Aware

VG SNP-Aware introduces an optimised alignment process for SNPs,  with the idea to  exploit  only  exact  alignments,  using  a  depth-limited  search  like.   The  process  is composed by two distinct phases: searching for initial node and mapping from it. Both phases are based on the characteristics of the variation graph in case of SNPs.

vg represents the two nucleotides of a biallelic SNP as two possible nodes on graph and for each of them provides an edge from and to it.  This approach allows to have multiple paths which admits all possible sequences on graph, including the reference base or the alternative base of SNPs.  The naming convention applied to the path requires alternative path having a name starting with ”alt”,  this allows to distinguish it from the reference paths. It is important to note that each node belongs to one or more paths.

![Variation graph](https://raw.githubusercontent.com/monsmau/vg/vg_snp_aware/doc/figures/variationgraph_with_SNP_ID_SEQ_PATH.png)

The construction of a variation graph starts from a reference genome and a set of SNPs. In case of SNPs, nodes creation is sequential into the forward direction. For the forward direction, the resulting node IDs are increasing, otherwise they are decreasing.  This allows VG SNP-Aware to follow up the consecutive nature of node IDs for an efficient mapping.


Algorithms for initial node search and for mapping have been included in the vg map command. Thus, VG SNP-Aware is a custom reimplementation of the map command of vg. 
VG SNP-Aware is able align reads exactly to a variation graph and detect SNPs based on these aligned reads. The results show that VG SNP-Aware can efficiently map
reads to a variation graph with a speed of 40x with respect to vg and similar accuracy on SNPs detection.

## Installation

### Building on Linux


First, obtain the repo:

    git clone https://github.com/monsmau/vg.git
    cd vg
    
Then, change branch:

    git checkout origin/vg_snp_aware
    
Obtain submodules:

    git submodule update --init --recursive

    
Then, install VG's dependencies. On Ubuntu, you should be able to do:

    make get-deps


On other distros, you will need to perform the equivalent of:

    sudo apt-get install build-essential git cmake pkg-config libncurses-dev libbz2-dev  \
                         protobuf-compiler libprotoc-dev libprotobuf-dev libjansson-dev \
                         automake libtool jq bc rs curl unzip redland-utils \
                         librdf-dev bison flex gawk lzma-dev liblzma-dev liblz4-dev \
                         libffi-dev libcairo-dev libboost-all-dev


Check that Protobuf version is at least 3.0.

Check that gcc version is at least 4.9.

Check that cmake version is at least 3.18.x (https://github.com/vgteam/vg/issues/3014).


Build VG including VG SNP-Aware:

    make static


## Usage

The VG SNP-Aware implementation follows the vg pipeline, the change has been made to the vg map command. 

![Variation graph](https://raw.githubusercontent.com/monsmau/vg/vg_snp_aware/doc/figures/vgpipelinecomplete.png)

In order  to  perform the  entire genotyping  process  the vg construct  and index steps are required to obtain the graphs.
 
The map command of VG SNP-Aware includes:
*  --snp-aware:  performs the alignment on graph with the VG SNP-Aware algorithm 
*  --printsnp: allows to reduce the output size to only reads with one or more reference or alternative base of SNPs

To use VG SNP-Aware the sequentialSearch parameter is required while printMin is an optional parameter but it is recommended. The -j parameter is mandatory to obtain as output the JSON mapping file. It is possible to use the vg view command to switch from JSON to GAM files and vice versa.


    vg map -f reads.fq -x graph.xg -g graph.gcsa --printsnp --snp-aware -j 
 
    vg map -f reads.fq -x graph.xg -g graph.gcsa --snp-aware -j


The  VG  version used by VG SNP-Aware is v1.29.0-44-ga74417fcb "Sospiro".

## Resources and datasets

| NAME  | DESCRIPTION |
| ------------- | ------------- |
| hg19  | Reference genome  |
| dbSNP  | Biallelic SNPs dataset  |
| NA12878  | Gold standard  |
| SRR622461  | Dataset of reads with coverage 6X  |
| SRR622457  | Dataset of reads with coverage 10X  |


| NAME  | LINK |
| ------------- | ------------- |
| hg19  | http://cb.csail.mit.edu/cb/lava/data/hg19.fa.gz |
| dbSNP  | http://cb.csail.mit.edu/cb/lava/data/SNPs142_hg19_Common.filt.txt |
| NA12878  | ftp://ftp-trace.ncbi.nlm.nih.gov/giab/ftp/release/NA12878_HG001/latest/GRCh37/  |
| SRR622461  | ftp://ftp.sra.ebi.ac.uk/vol1/fastq/SRR622/SRR622461/ |
| SRR622457  | ftp://ftp.sra.ebi.ac.uk/vol1/fastq/SRR622/SRR622457/  |


## Getting help
If you encounter bugs or have further questions or requests, you can raise an issue at the issue page. You can also contact Maurilio Monsù at maurilio.monsu@studenti.unipd.it


## Citation
 M. Monsù, M. Comin, "Fast Alignment of Reads to a Variation Graph with Application to SNP Detection", under submission.


