/*++


Module Name:

    AlignerContext.cpp

Abstract:

    Common parameters for running single & paired alignment.

Authors:

    Ravi Pandya, May, 2012

Environment:
`
    User mode service.

Revision History:

    Integrated from SingleAligner.cpp & PairedAligner.cpp

--*/

#include "stdafx.h"
#include "Compat.h"
#include "options.h"
#include "AlignerOptions.h"
#include "AlignerContext.h"
#include "AlignerStats.h"
#include "BaseAligner.h"
#include "FileFormat.h"
#include "exit.h"
#include "PairedAligner.h"

using std::max;
using std::min;

//
// Save the index & index directory globally so that we don't need to reload them on multiple runs.
//
GenomeIndex *g_index = NULL;
char *g_indexDirectory = NULL;

AlignerContext::AlignerContext(int i_argc, const char **i_argv, const char *i_version, AlignerExtension* i_extension)
    :
    index(NULL),
    writerSupplier(NULL),
    options(NULL),
    stats(NULL),
    extension(i_extension != NULL ? i_extension : new AlignerExtension()),
    readWriter(NULL),
    argc(i_argc),
    argv(i_argv),
    version(i_version),
    perfFile(NULL)
{
}

AlignerContext::~AlignerContext()
{
    delete extension;
    if (NULL != perfFile) {
        fclose(perfFile);
    }
}

void AlignerContext::runAlignment(int argc, const char **argv, const char *version, unsigned *argsConsumed)
{
    options = parseOptions(argc, argv, version, argsConsumed, isPaired());
#ifdef _MSC_VER
    useTimingBarrier = options->useTimingBarrier;
#endif

    initialize();
    extension->initialize();
    
    if (! extension->skipAlignment()) {
        printStatsHeader();
        do {

            beginIteration();

            runTask();
            
            finishIteration();
            
            printStats();

        } while (nextIteration());
    }

    extension->finishAlignment();
    PrintBigAllocProfile();
    PrintWaitProfile();
}

    void
AlignerContext::initializeThread()
{
    stats = newStats(); // separate copy per thread
    stats->extra = extension->extraStats();
    readWriter = writerSupplier != NULL ? writerSupplier->getWriter() : NULL;
    extension = extension->copy();
}

    void
AlignerContext::runThread()
{
    extension->beginThread();
    runIterationThread();
    if (readWriter != NULL) {
        readWriter->close();
        delete readWriter;
    }
    extension->finishThread();
}
    
    void
AlignerContext::finishThread(AlignerContext* common)
{
    common->stats->add(stats);
    delete stats;
    stats = NULL;
    delete extension;
    extension = NULL;
}

    void
AlignerContext::initialize()
{
    if (g_indexDirectory == NULL || strcmp(g_indexDirectory, options->indexDir) != 0) {
        delete g_index;
        g_index = NULL;
        delete g_indexDirectory;
        g_indexDirectory = new char [strlen(options->indexDir) + 1];
        strcpy(g_indexDirectory, options->indexDir);

        if (strcmp(options->indexDir, "-") != 0) {
            fprintf(stderr, "Loading index from directory... ");
 
            fflush(stdout);
            _int64 loadStart = timeInMillis();
            index = GenomeIndex::loadFromDirectory((char*) options->indexDir);
            if (index == NULL) {
                fprintf(stderr, "Index load failed, aborting.\n");
                soft_exit(1);
            }
            g_index = index;

            _int64 loadTime = timeInMillis() - loadStart;
             fprintf(stderr, "%llds.  %u bases, seed size %d\n",
                    loadTime / 1000, index->getGenome()->getCountOfBases(), index->getSeedLength());
         } else {
            fprintf(stderr, "no alignment, input/output only\n");
        }
    } else {
        index = g_index;
    }

    maxHits_ = options->maxHits;
    maxDist_ = options->maxDist;
    extraSearchDepth = options->extraSearchDepth;

    if (options->perfFileName != NULL) {
        perfFile = fopen(options->perfFileName,"a");
        if (NULL == perfFile) {
            fprintf(stderr,"Unable to open perf file '%s'\n", options->perfFileName);
            soft_exit(1);
        }
    }

    DataSupplier::ThreadCount = options->numThreads;
}

    void
AlignerContext::printStatsHeader()
{
    fprintf(stderr, "MaxHits\tMaxDist\t%%Used\t%%Unique\t%%Multi\t%%!Found\t%%Error\t%%Pairs\tlvCalls\tNumReads\tReads/s\n");
}

    void
AlignerContext::beginIteration()
{
    writerSupplier = NULL;
    alignStart = timeInMillis();
    clipping = options->clipping;
    totalThreads = options->numThreads;
    computeError = options->computeError;
    bindToProcessors = options->bindToProcessors;
    maxDist = maxDist_;
    maxHits = maxHits_;
    numSeedsFromCommandLine = options->numSeedsFromCommandLine;
    seedCoverage = options->seedCoverage;
    if (stats != NULL) {
        delete stats;
    }
    stats = newStats();
    stats->extra = extension->extraStats();
    extension->beginIteration();
    
    readerContext.clipping = options->clipping;
    readerContext.defaultReadGroup = options->defaultReadGroup;
    readerContext.genome = index != NULL ? index->getGenome() : NULL;
    readerContext.ignoreSecondaryAlignments = options->ignoreSecondaryAlignments;
    DataSupplier::ExpansionFactor = options->expansionFactor;
	readerContext.header = NULL;
	readerContext.headerLength = 0;
	readerContext.headerBytes = 0;

    typeSpecificBeginIteration();

    if (UnknownFileType != options->outputFile.fileType) {
        const FileFormat* format;
        if (SAMFile == options->outputFile.fileType) {
            format = FileFormat::SAM[options->useM];
        } else if (BAMFile == options->outputFile.fileType) {
            format = FileFormat::BAM[options->useM];
        } else {
            //
            // This shouldn't happen, because the command line parser should catch it.  Perhaps you've added a new output file format and just
            // forgoten to add it here.
            //
            fprintf(stderr, "AlignerContext::beginIteration(): unknown file type %d for '%s'\n", options->outputFile.fileType, options->outputFile.fileName);
            soft_exit(1);
        }

        writerSupplier = format->getWriterSupplier(options, readerContext.genome);
        ReadWriter* headerWriter = writerSupplier->getWriter();
        headerWriter->writeHeader(readerContext, options->sortOutput, argc, argv, version, options->rgLineContents);
        headerWriter->close();
        delete headerWriter;
    }
}

    void
AlignerContext::finishIteration()
{
    extension->finishIteration();

    if (NULL != writerSupplier) {
        writerSupplier->close();
        delete writerSupplier;
        writerSupplier = NULL;
    }

    alignTime = /*timeInMillis() - alignStart -- use the time from ParallelTask.h, that may exclude memory allocation time*/ time;
}

    bool
AlignerContext::nextIteration()
{
    //
    // This thing is a vestage of when we used to allow parameter ranges.
    //
    typeSpecificNextIteration();
    return false;
}

    void
AlignerContext::printStats()
{
    double usefulReads = max((double) stats->usefulReads, 1.0);
    char errorRate[16];
    if (options->computeError) {
        _int64 numSingle = max(stats->singleHits, (_int64) 1);
        snprintf(errorRate, sizeof(errorRate), "%0.3f%%", (100.0 * stats->errors) / numSingle);
    } else {
        snprintf(errorRate, sizeof(errorRate), "-");
    }
    fprintf(stderr, "%d\t%d\t%0.2f%%\t%0.2f%%\t%0.2f%%\t%0.2f%%\t%s\t%0.2f%%\t%lld\t%lld\t%.0f (at: %lld)\n",
            maxHits_, maxDist_, 
            100.0 * usefulReads / max(stats->totalReads, (_int64) 1),
            100.0 * stats->singleHits / usefulReads,
            100.0 * stats->multiHits / usefulReads,
            100.0 * stats->notFound / usefulReads,
            errorRate,
            100.0 * stats->alignedAsPairs / usefulReads,
            stats->lvCalls,
            stats->totalReads,
            (1000.0 * usefulReads) / max(alignTime, (_int64) 1), 
            alignTime);
    if (NULL != perfFile) {
        fprintf(perfFile, "%d\t%d\t%0.2f%%\t%0.2f%%\t%0.2f%%\t%0.2f%%\t%s\t%0.2f%%\t%lld\t%lld\tt%.0f\n",
                maxHits_, maxDist_, 
                100.0 * usefulReads / max(stats->totalReads, (_int64) 1),
                100.0 * stats->singleHits / usefulReads,
                100.0 * stats->multiHits / usefulReads,
                100.0 * stats->notFound / usefulReads,
                stats->lvCalls,
                errorRate,
                100.0 * stats->alignedAsPairs / usefulReads,
                stats->totalReads,
                (1000.0 * usefulReads) / max(alignTime, (_int64) 1));

        fprintf(perfFile,"\n");
    }
    // Running counts to compute a ROC curve (with error rate and %aligned above a given MAPQ)
    double totalAligned = 0;
    double totalErrors = 0;
    for (int i = AlignerStats::maxMapq; i >= 0; i--) {
        totalAligned += stats->mapqHistogram[i];
        totalErrors += stats->mapqErrors[i];
        double truePositives = (totalAligned - totalErrors) / max(stats->totalReads, (_int64) 1);
        double falsePositives = totalErrors / totalAligned;
        if (i <= 10 || i % 2 == 0 || i == 69) {
//            fprintf(stderr, "%d\t%d\t%d\t%.3f\t%.2E\n", i, stats->mapqHistogram[i], stats->mapqErrors[i], truePositives, falsePositives);
        }
    }

#if TIME_HISTOGRAM
    fprintf(stderr, "Per-read alignment time histogram:\nlog2(ns)\tcount\ttotal time (ns)\n");
    for (int i = 0; i < 31; i++) {
        fprintf(stderr, "%d\t%lld\t%lld\n", i, stats->countByTimeBucket[i], stats->nanosByTimeBucket[i]);
    }
#endif // TIME_HISTOGRAM


    stats->printHistograms(stdout);

#ifdef  TIME_STRING_DISTANCE
    fprintf(stderr, ""%llds, %lld calls in BSD noneClose, not -1\n",  stats->nanosTimeInBSD[0][1]/1000000000, stats->BSDCounts[0][1]);
    fprintf(stderr, ""%llds, %lld calls in BSD noneClose, -1\n",      stats->nanosTimeInBSD[0][0]/1000000000, stats->BSDCounts[0][0]);
    fprintf(stderr, ""%llds, %lld calls in BSD close, not -1\n",      stats->nanosTimeInBSD[1][1]/1000000000, stats->BSDCounts[1][1]);
    fprintf(stderr, ""%llds, %lld calls in BSD close, -1\n",          stats->nanosTimeInBSD[1][0]/1000000000, stats->BSDCounts[1][0]);
    fprintf(stderr, ""%llds, %lld calls in Hamming\n",                stats->hammingNanos/1000000000,         stats->hammingCount);
#endif  // TIME_STRING_DISTANCE

    extension->printStats();
}



        AlignerOptions*
AlignerContext::parseOptions(
    int i_argc,
    const char **i_argv,
    const char *i_version,
    unsigned *argsConsumed,
    bool      paired)
{
    argc = i_argc;
    argv = i_argv;
    version = i_version;

    AlignerOptions *options;

    if (paired) {
        options = new PairedAlignerOptions("snap paired <index-dir> <inputFile(s)> [<options>] where <input file(s)> is a list of files to process.\n");
    } else {
        options = new AlignerOptions("snap single <index-dir> <inputFile(s)> [<options>] where <input file(s)> is a list of files to process.\n");
    }

    options->extra = extension->extraOptions();
    if (argc < 2) {
        fprintf(stderr,"Too few parameters\n");
        options->usage();
    }

    options->indexDir = argv[0];
    struct InputList {
        SNAPFile    input;
        InputList*  next;
    } *inputList = NULL;

    //
    // Now build the input array and parse options.
    //

    bool inputFromStdio = false;

    int i;
    int nInputs = 0;
    for (i = 1; i < argc; i++) {

        if (',' == argv[i][0]  && '\0' == argv[i][1]) {
            i++;    // Consume the comma
            break;
        }

        int argsConsumed;
        SNAPFile input;
        if (SNAPFile::generateFromCommandLine(argv+i, argc-i, &argsConsumed, &input, paired, true)) {
            if (input.isStdio) {
                if (inputFromStdio) {
                    fprintf(stderr,"You specified stdin ('-') specified for more than one input, which isn't permitted.\n");
                    soft_exit(1);
                } else {
                    inputFromStdio = true;
                }
            }

            InputList *listEntry = new InputList;
            listEntry->input = input;
            listEntry->next = inputList;
            inputList = listEntry;      // Yes, this puts them in backwards.  a) We reverse them at the end and b) it doesn't matter anyway

            nInputs++;
            i += argsConsumed - 1;
            continue;
        }

        bool done;
        int oldI = i;
        if (!options->parse(argv, argc, i, &done)) {
            fprintf(stderr, "Didn't understand options starting at %s\n", argv[oldI]);
            options->usage();
        }

        if (done) {
            i++;    // For the ',' arg
            break;
        }
    }

    if (0 == nInputs) {
        fprintf(stderr,"No input files specified.\n");
        soft_exit(1);
    }

    if (options->maxDist + options->extraSearchDepth >= MAX_K) {
        fprintf(stderr,"You specified too large of a maximum edit distance combined with extra search depth.  The must add up to less than %d.\n", MAX_K);
        fprintf(stderr,"Either reduce their sum, or change MAX_K in LandauVishkin.h and recompile.\n");
        soft_exit(1);
    }

    options->nInputs = nInputs;
    options->inputs = new SNAPFile[nInputs];
    for (int j = nInputs - 1; j >= 0; j --) {
        // The loop runs backwards so that we reverse the reversing that we did when we built it.  Not that it matters anyway.
        _ASSERT(NULL != inputList);
        options->inputs[j] = inputList->input;
        InputList *dying = inputList;
        inputList = inputList->next;
        delete dying;
    }
    _ASSERT(NULL == inputList);

    *argsConsumed = i;
    return options;
}