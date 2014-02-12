/*++

Module Name:

    AlignerOptions.cpp

Abstract:

    Common parameters for running single & paired alignment.

Authors:

    Ravi Pandya, May, 2012

Environment:

    User mode service.

Revision History:

    Integrated from SingleAligner.cpp & PairedAligner.cpp

--*/

#include "stdafx.h"
#include "options.h"
#include "AlignerOptions.h"
#include "FASTQ.h"
#include "SAM.h"
#include "Bam.h"
#include "exit.h"


AlignerOptions::AlignerOptions(
    const char* i_commandLine,
    bool forPairedEnd)
    :
    commandLine(i_commandLine),
    indexDir(NULL),
    similarityMapFile(NULL),
    numThreads(GetNumberOfProcessors()),
    computeError(false),
    bindToProcessors(false),
    ignoreMismatchedIDs(false),
    clipping(ClipBack),
    sortOutput(false),
    noIndex(false),
    noDuplicateMarking(false),
    noQualityCalibration(false),
    sortMemory(0),
    filterFlags(0),
    explorePopularSeeds(false),
    stopOnFirstHit(false),
	useM(false),
    gapPenalty(0),
    misalignThreshold(15),
	extra(NULL),
    rgLineContents(NULL),
    perfFileName(NULL),
    useTimingBarrier(false),
    extraSearchDepth(2),
    defaultReadGroup("FASTQ"),
    seedCountSpecified(false),
    numSeedsFromCommandLine(0),
    ignoreSecondaryAlignments(true),
    outputMultipleAlignments(false),
    preserveClipping(false),
    expansionFactor(1.0)
{
    if (forPairedEnd) {
        maxDist                 = 15;
        seedCoverage            = 0;
        numSeedsFromCommandLine = 8;
        maxHits                 = 16000;
     } else {
        maxDist                 = 14;
        numSeedsFromCommandLine = 25;
        maxHits                 = 300;
    }

    initializeLVProbabilitiesToPhredPlus33();
}

    void
AlignerOptions::usage()
{
    usageMessage();
    soft_exit(1);
}

    void
AlignerOptions::usageMessage()
{
    fprintf(stderr,
        "Usage: \n%s\n"
        "Options:\n"
        "  -o   filename  output alignments to filename in SAM or BAM format, depending on the file extension or\n"
        "       explicit type specifier (see below)\n"
        "  -d   maximum edit distance allowed per read or pair (default: %d)\n"
        "  -n   number of seeds to use per read\n"
        "  -sc  Seed coverage (i.e., readSize/seedSize).  Floating point.  Exclusive with -n.  (default: %lf)\n"
        "  -h   maximum hits to consider per seed (default: %d)\n"
        "  -c   Deprecated parameter; this is ignored.  Consumes one extra arg.\n"
        "  -a   Deprecated parameter; this is ignored.  Consumes one extra arg.\n"
        "  -t   number of threads (default is one per core)\n"
        "  -b   bind each thread to its processor (off by default)\n"
        "  -e   compute error rate assuming wgsim-generated reads\n"
        "  -P   disables cache prefetching in the genome; may be helpful for machines\n"
        "       with small caches or lots of cores/cache\n"
        "  -so  sort output file by alignment location\n"
        "  -sm  memory to use for sorting in Gb\n"
        "  -x   explore some hits of overly popular seeds (useful for filtering)\n"
        "  -f   stop on first match within edit distance limit (filtering mode)\n"
        "  -F   filter output (a=aligned only, s=single hit only, u=unaligned only)\n"
        "  -S   suppress additional processing (sorted BAM output only)\n"
        "       i=index, d=duplicate marking\n"
#if     USE_DEVTEAM_OPTIONS
        "  -I   ignore IDs that don't match in the paired-end aligner\n"
        "  -E   misalign threshold (min distance from correct location to count as error)\n"
#ifdef  _MSC_VER    // Only need this on Windows, since memory allocation is fast on Linux
        "  -B   Insert barrier after per-thread memory allocation to improve timing accuracy\n"
#endif  // _MSC_VER
#endif  // USE_DEVTEAM_OPTIONS
        "  -Cxx must be followed by two + or - symbols saying whether to clip low-quality\n"
        "       bases from front and back of read respectively; default: back only (-C-+)\n"
		"  -M   indicates that CIGAR strings in the generated SAM file should use M (alignment\n"
		"       match) rather than = and X (sequence (mis-)match)\n"
        "  -G   specify a gap penalty to use when generating CIGAR strings\n"
        "  -pf  specify the name of a file to contain the run speed\n"
        "  --hp Indicates not to use huge pages (this may speed up index load and slow down alignment)\n"
        "  -D   Specifies the extra search depth (the edit distance beyond the best hit that SNAP uses to compute MAPQ).  Default 2\n"
        "  -rg  Specify the default read group if it is not specified in the input file\n"
        "  -sa  Include reads from SAM or BAM files with the secondary alignment (0x100) flag set; default is to drop them.\n"
        "  -om  Output multiple equivalent alignment locations if they exist\n"
        "  -pc  Preserve the soft clipping for reads coming from SAM or BAM files\n"
        "  -xf  Increase expansion factor for BAM and GZ files (default %.1f)\n"
            ,
            commandLine,
            maxDist,
            seedCoverage,
            maxHits,
            expansionFactor);

    if (extra != NULL) {
        extra->usageMessage();
    }

    fprintf(stderr, "\n\n"
                "You may process more than one alignment without restarting SNAP, and if possible without reloading\n"
                "the index.  In order to do this, list on the command line all of the parameters for the first\n"
                "alignment, followed by a comma (separated by a space from the other parameters) followed by the\n"
                "parameters for the next alignment (including single or paired).  You may have as many of these\n"
                "as you please.  If two consecutive alignments use the same index, it will not be reloaded.\n"
                "So, for example, you could do 'snap single hg19-20 foo.fq -o foo.sam , paired hg19-20 end1.fq end2.fq -o paired.sam'\n"
                "and it would not reload the index between the single and paired alignments.\n",
                "SNAP doesn't parse the options for later runs until the earlier ones have completed, so if you make\n"
                "an error in one, it may take a while for you to notice.  So, be careful (or check back shortly after\n"
                "you think each run will have completed).\n\n");

    fprintf(stderr, "When specifying an input or output file, you can simply list the filename, in which case\n");
    fprintf(stderr, "SNAP will infer the type of the file from the file extension (.sam or .bam for example),\n");
    fprintf(stderr, "or you can explicitly specify the file type by preceeding the filename with one of the\n");
    fprintf(stderr," following type specifiers (which are case sensitive):\n");
    fprintf(stderr,"    -fastq\n");
    fprintf(stderr,"    -compressedFastq\n");
    fprintf(stderr,"    -sam\n");
    fprintf(stderr,"    -bam\n");
    fprintf(stderr,"    -pairedFastq\n");
    fprintf(stderr,"    -pairedCompressedFastq\n");
    fprintf(stderr,"    -pairedInterleavedFastq\n");
    fprintf(stderr,"    -pairedCompressedInterleavedFastq\n");
    fprintf(stderr,"\nSo, for example, you could specify -bam input.file to make SNAP treat input.file as a BAM file,\n");
    fprintf(stderr,"even though it would ordinarily assume a FASTQ file for input or a SAM file for output when it\n");
    fprintf(stderr,"doens't recoginize the file extension.\n");
    fprintf(stderr,"In order to use a file name that begins with a '-' and not have SNAP treat it as a switch, you must\n");
    fprintf(stderr,"explicitly specify the type.  But really, that's just confusing and you shouldn't do it.\n");
}

    bool
AlignerOptions::parse(
    const char** argv,
    int argc,
    int& n,
    bool *done)
{
    *done = false;

    if (strcmp(argv[n], "-d") == 0) {
        if (n + 1 < argc) {
            maxDist = atoi(argv[n+1]);
            n++;
            return true;
        }
    } else if (strcmp(argv[n], "-n") == 0) {
        if (n + 1 < argc) {
            if (seedCountSpecified) {
                fprintf(stderr,"-sc and -n are mutually exclusive.  Please use only one.\n");
                soft_exit(1);
            }
            seedCountSpecified = true;
            numSeedsFromCommandLine = atoi(argv[n+1]);
            n++;
            return true;
        }
    } else if (strcmp(argv[n], "-sc") == 0) {
        if (n + 1 < argc) {
            if (seedCountSpecified) {
                fprintf(stderr,"-sc and -n are mutually exclusive.  Please use only one.\n");
                soft_exit(1);
            }
            seedCountSpecified = true;
            seedCoverage = atof(argv[n+1]);
            numSeedsFromCommandLine = 0;
            n++;
            return true;
        }
    } else if (strcmp(argv[n], "-h") == 0) {
        if (n + 1 < argc) {
            maxHits = atoi(argv[n+1]);
            n++;
            return true;
        }
    } else if (strcmp(argv[n], "-c") == 0) { // conf diff is deprecated, but we just ignore it rather than throwing an error.
        if (n + 1 < argc) {
            n++;
            return true;
        }
    } else if (strcmp(argv[n], "-a") == 0) { // adaptive conf diff is deprecated, but we just ignore it rather than throwing an error.
        if (n + 1 < argc) {
            n++;
            return true;
        }
    } else if (strcmp(argv[n], "-t") == 0) {
        if (n + 1 < argc) {
            numThreads = atoi(argv[n+1]);
            n++;
            return true;
        }
    } else if (strcmp(argv[n], "-o") == 0) {
        int argsConsumed;
        if (!SNAPFile::generateFromCommandLine(argv + n + 1, argc - n - 1, &argsConsumed, &outputFile, false, false)) {
            fprintf(stderr,"Must have a file specifier after -o\n");
            soft_exit(1);
        }
        n += argsConsumed;
        return true;
    } else if (strcmp(argv[n], "-e") == 0) {
        computeError = true;
        return true;
    } else if (strcmp(argv[n], "-P") == 0) {
        doAlignerPrefetch = false;
        return true;
    } else if (strcmp(argv[n], "-b") == 0) {
        bindToProcessors = true;
        return true;
    } else if (strcmp(argv[n], "-so") == 0) {
        sortOutput = true;
        return true;
    } else if (strcmp(argv[n], "-S") == 0) {
        if (n + 1 < argc) {
            n++;
            for (const char* p = argv[n]; *p; p++) {
                switch (*p) {
                case 'i':
                    noIndex = true;
                    break;
                case 'd':
                    noDuplicateMarking = true;
                    break;
                case 'q':
                    noQualityCalibration = true;
                    break;
                default:
                    return false;
                }
            }
            return true;
        }
    } else if (strcmp(argv[n], "-sm") == 0) {
        if (n + 1 < argc && argv[n+1][0] >= '0' && argv[n+1][0] <= '9') {
            sortMemory = atoi(argv[n+1]);
            n++;
            return true;
        }
    } else if (strcmp(argv[n], "-F") == 0) {
        if (n + 1 < argc) {
            n++;
            if (strcmp(argv[n], "a") == 0) {
                filterFlags = FilterSingleHit | FilterMultipleHits;
            } else if (strcmp(argv[n], "s") == 0) {
                filterFlags = FilterSingleHit;
            } else if (strcmp(argv[n], "u") == 0) {
                filterFlags = FilterUnaligned;
            } else {
                return false;
            }
            return true;
        }
    } else if (strcmp(argv[n], "-x") == 0) {
        explorePopularSeeds = true;
        return true;
    } else if (strcmp(argv[n], "-f") == 0) {
        stopOnFirstHit = true;
        return true;
#if     USE_DEVTEAM_OPTIONS
    } else if (strcmp(argv[n], "-I") == 0) {
        ignoreMismatchedIDs = true;
        return true;
    } else if (strcmp(argv[n], "-E") == 0) {
        if (n + 1 < argc) {
            misalignThreshold = atoi(argv[n+1]);
            n++;
            return true;
        }
#ifdef  _MSC_VER
    } else if (strcmp(argv[n], "-B") == 0) {
        useTimingBarrier = true;
        return true;
#endif  // _MSC_VER
#endif  // USE_DEVTEAM_OPTIONS
	} else if (strcmp(argv[n], "-M") == 0) {
		useM = true;
		return true;
	} else if (strcmp(argv[n], "-sa") == 0) {
		ignoreSecondaryAlignments = false;
		return true;
	} else if (strcmp(argv[n], "-om") == 0) {
		outputMultipleAlignments = true;
		return true;
	} else if (strcmp(argv[n], "-xf") == 0) {
        if (n + 1 < argc) {
            n++;
            expansionFactor = (float)atof(argv[n]);
            return expansionFactor > 0;
        }
	} else if (strcmp(argv[n], "-pc") == 0) {
		preserveClipping = true;
		return true;
	} else if (strcmp(argv[n], "-G") == 0) {
        if (n + 1 < argc) {
            gapPenalty = atoi(argv[n+1]);
            if (gapPenalty < 1) {
                fprintf(stderr,"Gap penalty must be at least 1.\n");
                soft_exit(1);
            }
            n++;
            return true;
        } else {
            fprintf(stderr,"Must have the gap penalty value after -G\n");
        }
    } else if (strcmp(argv[n], "-r") == 0) {
#if 0   // This isn't ready yet.
        if (n + 1 < argc) {
            //
            // Check the line for sanity.  It must consist either of @RG\t<fields> or just <fields> (in which
            // case we add the @RG part).  It must contain a field called ID. Fields are separated by tabs.
            // We don't require that the fields be things that are listed in the SAM spec, however, because
            // new ones might be added.
            //
            const char *parsePointer = argv[n+1];
            if (*parsePointer == '@') {
            }

            rgLineContents = argv[n+1];
            n++;
            return true;
        }
#endif  // 0
	} else if (strcmp(argv[n], "-pf") == 0) {
        if (n + 1 < argc) {
            perfFileName = argv[n+1];
            n++;
            return true;
        } else {
            fprintf(stderr,"Must specify the name of the perf file after -pf\n");
        }
	} else if (strcmp(argv[n], "-rg") == 0) {
        if (n + 1 < argc) {
            defaultReadGroup = argv[n+1];
            n++;
            char* s = new char[1 + strlen(defaultReadGroup) + strlen("@RG\tID:\tSM:sample")];
            sprintf(s, "@RG\tID:%s\tSM:sample", defaultReadGroup);
            rgLineContents = s;
            return true;
        } else {
            fprintf(stderr,"Must specify the default read group after -rg\n");
        }
    } else if (strcmp(argv[n], "--hp") == 0) {
        BigAllocUseHugePages = false;
        return true;
	} else if (strcmp(argv[n], "-D") == 0) {
        if (n + 1 < argc) {
            extraSearchDepth = atoi(argv[n+1]);
            n++;
            return true;
        } else {
            fprintf(stderr,"Must specify the desired extra search depth after -D\n");
        }
    } else if (strlen(argv[n]) >= 2 && '-' == argv[n][0] && 'C' == argv[n][1]) {
        if (strlen(argv[n]) != 4 || '-' != argv[n][2] && '+' != argv[n][2] ||
            '-' != argv[n][3] && '+' != argv[n][3]) {

            fprintf(stderr,"Invalid -C argument.\n\n");
            return false;
        }

        if ('-' == argv[n][2]) {
            if ('-' == argv[n][3]) {
                clipping = NoClipping;
            } else {
                clipping = ClipBack;
            }
        } else {
            if ('-' == argv[n][3]) {
                clipping = ClipFront;
            } else {
                clipping = ClipFrontAndBack;
            }
        }
        return true;
    } else if (strcmp(argv[n], ",") == 0) {
        //
        // End of args for this run.
        //
        *done = true;
        return true;
    } else if (extra != NULL) {
        return extra->parse(argv, argc, n, done);
    }
    return false;
}

    bool
AlignerOptions::passFilter(
    Read* read,
    AlignmentResult result)
{
    if (filterFlags == 0) {
        return true;
    }
    switch (result) {
    case NotFound:
    case UnknownAlignment:
        return (filterFlags & FilterUnaligned) != 0;
    case SingleHit:
        return (filterFlags & FilterSingleHit) != 0;
    case MultipleHits:
        return (filterFlags & FilterMultipleHits) != 0;
    default:
        return false; // shouldn't happen!
    }
}

    void
SNAPFile::readHeader(ReaderContext& context)
{
    switch (fileType) {
    case SAMFile:
        return SAMReader::readHeader(fileName, context);
        
    case BAMFile:
        return BAMReader::readHeader(fileName, context);

    case FASTQFile:
        return FASTQReader::readHeader(fileName,  context);
        
    case InterleavedFASTQFile:
        return PairedInterleavedFASTQReader::readHeader(fileName,  context);

    default:
        _ASSERT(false);
    }
}

    PairedReadSupplierGenerator *
SNAPFile::createPairedReadSupplierGenerator(int numThreads, bool quicklyDropUnpairedReads, const ReaderContext& context)
{
    _ASSERT(fileType == SAMFile || fileType == BAMFile || fileType == InterleavedFASTQFile || secondFileName != NULL); // Caller's responsibility to check this

    switch (fileType) {
    case SAMFile:
        return SAMReader::createPairedReadSupplierGenerator(fileName, numThreads, quicklyDropUnpairedReads, context);
        
    case BAMFile:
        return BAMReader::createPairedReadSupplierGenerator(fileName,numThreads, quicklyDropUnpairedReads, context);

    case FASTQFile:
        return PairedFASTQReader::createPairedReadSupplierGenerator(fileName, secondFileName, numThreads, context, isCompressed);

    case InterleavedFASTQFile:
        return PairedInterleavedFASTQReader::createPairedReadSupplierGenerator(fileName, numThreads, context, isCompressed);
        
    default:
        _ASSERT(false);
        fprintf(stderr,"SNAPFile::createPairedReadSupplierGenerator: invalid file type (%d)\n", fileType);
        soft_exit(1);
        return NULL;
    }
}

    ReadSupplierGenerator *
SNAPFile::createReadSupplierGenerator(int numThreads, const ReaderContext& context)
{
    _ASSERT(secondFileName == NULL);
    switch (fileType) {
    case SAMFile:
        return SAMReader::createReadSupplierGenerator(fileName, numThreads, context);
        
    case BAMFile:
        return BAMReader::createReadSupplierGenerator(fileName,numThreads, context);

    case FASTQFile:
        return FASTQReader::createReadSupplierGenerator(fileName, numThreads, context, isCompressed);

    default:
        _ASSERT(false);
        fprintf(stderr,"SNAPFile::createReadSupplierGenerator: invalid file type (%d)\n", fileType);
        soft_exit(1);
        return NULL;
    }
}

        bool 
SNAPFile::generateFromCommandLine(const char **args, int nArgs, int *argsConsumed, SNAPFile *snapFile, bool paired, bool isInput)
{
    snapFile->fileName = NULL;
    snapFile->secondFileName = NULL;
    snapFile->isCompressed = false;
    *argsConsumed = 0;
    snapFile->isStdio = false;

    if (0 == nArgs) {
        return false;
    }

    //
    // Check to see if this is an explicit file type.
    //
    if ('-' == args[0][0] && '\0' != args[0][1]) { // starts with - but isn't just a - (which means to use stdio without a type specifier)
        if (1 == nArgs) {
            return false;
        }

        if (!strcmp(args[1], "-")) {
            snapFile->isStdio = true;
        }

        if (!strcmp(args[0], "-fastq") || !strcmp(args[0], "-compressedFastq")) {
            if (!isInput) {
                fprintf(stderr,"%s is not a valid output file type.\n", args[0]);
                soft_exit(1);
            }

            if (paired && nArgs < 3) {
                fprintf(stderr,"Expected a pair of fastQ files, but instead just got one\n");
                soft_exit(1);
            }

 
            snapFile->isCompressed = !strcmp(args[0], "-compressedFastq");

            if (paired) {
                snapFile->fileType = FASTQFile;
                snapFile->secondFileName = args[2];
                if (!strcmp("-", args[2])) {
                    if (snapFile->isStdio) {
                        fprintf(stderr,"Can't have both halves of paired FASTQ files be stdin ('-').  Did you mean to use the interleaved FASTQ type?\n");
                        soft_exit(1);
                    }
                    snapFile->isStdio = true;
                }
                *argsConsumed = 3;
            } else {
               snapFile->fileType = FASTQFile;
               *argsConsumed = 2;
            }
        } else if (!strcmp(args[0], "-sam")) {
            snapFile->fileType = SAMFile;
            *argsConsumed = 2;
        } else if (!strcmp(args[0], "-bam")) {
            snapFile->fileType = BAMFile;
            snapFile->isCompressed = true;
            *argsConsumed = 2;
        } else if (!strcmp(args[0], "-pairedInterleavedFastq") || !strcmp(args[0], "-pairedCompressedInterleavedFastq")) {
            if (!paired) {
                fprintf(stderr,"Specified %s for a single-end alignment.  To treat it as single-end, just use ordinary fastq (or compressed fastq, as appropriate)\n", args[0]);
                soft_exit(1);
            }

            snapFile->fileType = InterleavedFASTQFile;
            snapFile->isCompressed = !strcmp(args[0], "-pairedCompressedInterleavedFastq");
            *argsConsumed = 2;
        } else {
            //
            // starts with '-' but isn't a file-type specifier
            //
            return false;
        }
        snapFile->fileName = args[1];
        return true;
    }

    //
    // Just a filename.  Infer the type.
    //

    *argsConsumed = 1;
    snapFile->fileName = args[0];
    snapFile->isStdio = '-' == args[0][0] && '\0' == args[0][1];

    if (util::stringEndsWith(args[0], ".sam")) {
        snapFile->fileType = SAMFile;
        snapFile->isCompressed = false;
    } else if (util::stringEndsWith(args[0], ".bam")) {
        snapFile->fileType = BAMFile;
        snapFile->isCompressed = true;
    } else if (!isInput) {
        //
        // No default output file type.
        //
        fprintf(stderr,"You specified an output file with name '%s', which doesn't end in .sam or .bam, and doesn't have an explicit type\n", args[0]);
        fprintf(stderr,"specifier.  There is no default output file type.  Consider doing something like '-o -bam %s'\n", args[0]);
        soft_exit(1);
    } else if (util::stringEndsWith(args[0], ".fq") || util::stringEndsWith(args[0], ".fastq") ||
        util::stringEndsWith(args[0], ".fq.gz") || util::stringEndsWith(args[0], ".fastq.gz") ||
        util::stringEndsWith(args[0], ".fq.gzip") || util::stringEndsWith(args[0], ".fastq.gzip")) {

        // 
        // It's a fastq input file (either by default or because it's got a .fq or .fastq extension, we don't
        // need to check).  See if it's also compressed.
        //
        snapFile->fileType= FASTQFile;
        if (util::stringEndsWith(args[0], ".gz") || util::stringEndsWith(args[0], ".gzip")) {
            snapFile->isCompressed = true;
        } else {
            snapFile->isCompressed = false;
        }

        snapFile->isStdio = !strcmp(args[0], "-");

        if (paired) {
            snapFile->secondFileName = args[1];
            if (!strcmp(args[1], "-")) {
                if (snapFile->isStdio) {
                    fprintf(stderr,"Can't have both halves of paired FASTQ files be stdin ('-').  Did you mean to use the interleaved FASTQ type?\n");
                    soft_exit(1);
                }
                snapFile->isStdio = true;
            }

            *argsConsumed = 2;
        }


    } else {
        if (snapFile->isStdio) {
            fprintf(stderr,"Stdio IO always requires an explicit file type.  So, for example, do 'snap single index-directory -fastq -' to read FASTQ from stdin\n");
        } else {
            fprintf(stderr, "Unknown file type, please specify file type with -fastq, -sam, -bam, etc.\n");
        }

        soft_exit(1);
    }

    return true;
}
