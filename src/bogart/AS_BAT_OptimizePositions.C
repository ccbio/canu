
/******************************************************************************
 *
 *  This file is part of canu, a software program that assembles whole-genome
 *  sequencing reads into contigs.
 *
 *  This software is based on:
 *    'Celera Assembler' (http://wgs-assembler.sourceforge.net)
 *    the 'kmer package' (http://kmer.sourceforge.net)
 *  both originally distributed by Applera Corporation under the GNU General
 *  Public License, version 2.
 *
 *  Canu branched from Celera Assembler at its revision 4587.
 *  Canu branched from the kmer project at its revision 1994.
 *
 *  This file is derived from:
 *
 *    src/bogart/AS_BAT_Unitig.C
 *
 *  Modifications by:
 *
 *    Brian P. Walenz beginning on 2017-JUL-17
 *      are a 'United States Government Work', and
 *      are released in the public domain
 *
 *  File 'README.licenses' in the root directory of this distribution contains
 *  full conditions and disclaimers for each license.
 */

#include "AS_global.H"
#include "AS_BAT_Unitig.H"
#include "AS_BAT_ReadInfo.H"
#include "AS_BAT_BestOverlapGraph.H"
#include "AS_BAT_Logging.H"



class optPos {
public:
  optPos() {
  };
  ~optPos() {
  };

  void    set(ufNode &n) {
    ident = n.ident;
    min   = n.position.min();
    max   = n.position.max();
    fwd   = n.position.isForward();
  };

  uint32  ident;
  double  min;
  double  max;
  bool    fwd;
};



void
Unitig::optimize_initPlace(uint32        ii,
                           optPos       *op,
                           optPos       *np,
                           bool          firstPass,
                           set<uint32>  &failed,
                           bool          beVerbose) {
  uint32   iid  = ufpath[ii].ident;
  double   nmin = 0;
  int32    cnt  = 0;

  if ((firstPass == false) && (failed.count(iid) == 0))  //  If the second pass and not
    return;                                              //  failed, do nothing.

  if (firstPass == false)
    writeLog("optimize_initPlace()-- Second pass begins.\n");

  //  Then process all overlaps.

  if (ii > 0) {
    uint32       ovlLen  = 0;
    BAToverlap  *ovl     = OC->getOverlaps(iid, ovlLen);

    for (uint32 oo=0; oo<ovlLen; oo++) {
      uint32  jid = ovl[oo].b_iid;
      uint32  uu  = inUnitig (jid);
      uint32  jj  = ufpathIdx(jid);

      //  Probably overkill, but report ALL overlaps for the troubling reads.

      if ((beVerbose) || (firstPass == false))
        writeLog("optimize_initPlace()-- olap %u a %u b %u hangs %d %d\n", oo, iid, jid, ovl[oo].a_hang, ovl[oo].b_hang);

      if (uu != id())   //  Skip if the overlap is to a different tig.
        continue;       //  (the ufpathIdx() call is valid, but using it isn't)

      //  Reads are in the same tig.  Decide if they overlap in position.

      bool  isOvl = isOverlapping(ufpath[ii].position, ufpath[jj].position);

      //  Log!  beVerbose should be true for the second pass, but just in case it isn't.

      if ((beVerbose) || (firstPass == false))
        writeLog("optimize_initPlace()-- olap %4u tig %7u read %8u (at %9d %9d) olap to read %8u (at %9d %9d) - hangs %7d %7d - %s %s\n",
                 oo, id(),
                 iid, ufpath[ii].position.bgn, ufpath[ii].position.end,
                 jid, ufpath[jj].position.bgn, ufpath[jj].position.end,
                 ovl[oo].a_hang, ovl[oo].b_hang,
                 (isOvl == true) ? "overlapping" : "not-overlapping",
                 (jj > ii)       ? "after"       : "before");

      if (isOvl == false)            //  Skip if the reads
        continue;                    //  don't overlap

      if ((firstPass) && (jj > ii))  //  We're setting initial positions, so overlaps to reads after
        continue;                    //  us aren't correct, unless we're in the 2nd pass

      //  Reads overlap.  Compute the position of the read using
      //  the overlap and the other read.

      nmin += (op[iid].fwd) ? (op[jid].min - ovl[oo].a_hang) : (op[jid].min + ovl[oo].b_hang);
      cnt  += 1;
    }  //  over all overlaps

    //  If no overlaps found, flag this read for a second pass.  If in the second pass,
    //  not much we can do.

    if ((firstPass == true) && (cnt == 0)) {
      writeLog("optimize_initPlace()-- Failed to find overlaps for read %u in tig %u at %d-%d (first pass)\n",
               iid, id(), ufpath[ii].position.bgn, ufpath[ii].position.end);
      failed.insert(iid);
      return;
    }

    if ((firstPass == false) && (cnt == 0)) {
      writeLog("optimize_initPlace()-- Failed to find overlaps for read %u in tig %u at %d-%d (second pass)\n",
               iid, id(), ufpath[ii].position.bgn, ufpath[ii].position.end);
      flushLog();
    }

    assert(cnt > 0);
  }

  //  The initialization above does very little to enforce read lengths, and the optimization
  //  doesn't put enough weight in the read length to make it stable.  We simply force
  //  the correct read length here.

  op[iid].min = (cnt == 0) ? 0 : (nmin / cnt);
  op[iid].max = op[iid].min + RI->readLength(ufpath[ii].ident);

  np[iid].min = 0;
  np[iid].max = 0;

  if (beVerbose)
    writeLog("optimize_initPlace()-- tig %7u read %9u initialized to position %9.2f %9.2f%s\n",
             id(), op[iid].ident, op[iid].min, op[iid].max, (firstPass == true) ? "" : " SECONDPASS");
}



void
Unitig::optimize_recompute(uint32        iid,
                           optPos       *op,
                           optPos       *np,
                           bool          beVerbose) {
  uint32       ii      = ufpathIdx(iid);

  int32        readLen = RI->readLength(iid);

  uint32       ovlLen  = 0;
  BAToverlap  *ovl     = OC->getOverlaps(iid, ovlLen);

  double       nmin = 0.0;
  double       nmax = 0.0;
  uint32       cnt  = 0;

  if (beVerbose) {
    writeLog("optimize()-- tig %8u read %8u previous  - %9.2f-%-9.2f\n", id(), iid, op[iid].min,           op[iid].max);
    writeLog("optimize()-- tig %8u read %8u length    - %9.2f-%-9.2f\n", id(), iid, op[iid].max - readLen, op[iid].min + readLen);
  }

  //  Process all overlaps.

  for (uint32 oo=0; oo<ovlLen; oo++) {
    uint32  jid = ovl[oo].b_iid;
    uint32  uu  = inUnitig (jid);
    uint32  jj  = ufpathIdx(jid);

    if (uu != id())   //  Skip if the overlap is to a different tig.
      continue;       //  (the ufpathIdx() call is valid, but using it isn't)

    if (isOverlapping(ufpath[ii].position, ufpath[jj].position) == false)  //  Skip if the reads
      continue;                                                            //  don't overlap

    //  Reads overlap.  Compute the position of the read using
    //  the overlap and the other read.

    double tmin = (op[iid].fwd) ? (op[jid].min - ovl[oo].a_hang) : (op[jid].min + ovl[oo].b_hang);
    double tmax = (op[iid].fwd) ? (op[jid].max - ovl[oo].b_hang) : (op[jid].max + ovl[oo].a_hang);

    if (beVerbose)
      writeLog("optimize()-- tig %8u read %8u olap %4u - %9.2f-%-9.2f\n", id(), iid, oo, tmin, tmax);

    nmin += tmin;
    nmax += tmax;
    cnt  += 1;
  }  //  over all overlaps

  //  Add in some evidence for the bases in the read.  We want higher weight than the overlaps,
  //  but not enough to swamp the hangs.

  nmin   += cnt/4 * (op[iid].max - readLen);
  nmax   += cnt/4 * (op[iid].min + readLen);
  cnt    += cnt/4;

  //  Find the average and save.

  np[iid].min = nmin / cnt;
  np[iid].max = nmax / cnt;

  double dmin = 2 * (op[iid].min - np[iid].min) / (op[iid].min + np[iid].min);
  double dmax = 2 * (op[iid].max - np[iid].max) / (op[iid].max + np[iid].max);
  double npll = np[iid].max - np[iid].min;

  if (beVerbose)
    writeLog("optimize()-- tig %8u read %8u           - %9.2f-%-9.2f length %9.2f/%-6d %7.2f%% posChange %+6.4f %+6.4f\n",
             id(), iid,
             np[iid].min, np[iid].max,
             npll, readLen,
             200.0 * (npll - readLen) / (npll + readLen),
             dmin, dmax);
}





void
Unitig::optimize_expand(optPos  *op) {

  for (uint32 ii=0; ii<ufpath.size(); ii++) {
    uint32       iid     = ufpath[ii].ident;

    int32        readLen = RI->readLength(iid);

    double       opiimin = op[iid].min;             //  New start of this read, same as the old start
    double       opiimax = op[iid].min + readLen;   //  New end of this read
    double       opiilen = op[iid].max - op[iid].min;

    if (readLen <= opiilen)   //  This read is sufficiently long,
      continue;               //  do nothing.

    double       scale   = readLen / opiilen;
    double       expand  = opiimax - op[iid].max;   //  Amount we changed this read, bases

    //  For each read, adjust positions based on how much they overlap with this read.

    for (uint32 jj=0; jj<ufpath.size(); jj++) {
      uint32 jid = ufpath[jj].ident;

      if      (op[jid].min < op[iid].min)
        ;
      else if (op[jid].min < op[iid].max)
        op[jid].min  = opiimin + (op[jid].min - op[iid].min) * scale;
      else
        op[jid].min += expand;


      if      (op[jid].max < op[iid].min)
        ;
      else if (op[jid].max < op[iid].max)
        op[jid].max  = opiimin + (op[jid].max - op[iid].min) * scale;
      else
        op[jid].max += expand;
    }

    //  Finally, actually shift us

    op[iid].min = opiimin;
    op[iid].max = opiimax;
  }
}



void
Unitig::optimize_setPositions(optPos  *op,
                              bool     beVerbose) {

  for (uint32 ii=0; ii<ufpath.size(); ii++) {
    uint32  iid     = ufpath[ii].ident;

    int32   readLen = RI->readLength(iid);
    int32   opll    = (int32)op[iid].max - (int32)op[iid].min;
    double  opdd    = 200.0 * (opll - readLen) / (opll + readLen);

    if (op[iid].fwd) {
      if (beVerbose)
        writeLog("optimize()-- read %8u -> from %9d,%-9d %7d to %9d,%-9d %7d readLen %7d diff %7.4f%%\n",
                 iid,
                 ufpath[ii].position.bgn,
                 ufpath[ii].position.end,
                 ufpath[ii].position.end - ufpath[ii].position.bgn,
                 (int32)op[iid].min,
                 (int32)op[iid].max,
                 opll,
                 readLen,
                 opdd);

      ufpath[ii].position.bgn = (int32)op[iid].min;
      ufpath[ii].position.end = (int32)op[iid].max;
    } else {
      if (beVerbose)
        writeLog("optimize()-- read %8u <- from %9d,%-9d %7d to %9d,%-9d %7d readLen %7d diff %7.4f%%\n",
                 iid,
                 ufpath[ii].position.bgn,
                 ufpath[ii].position.end,
                 ufpath[ii].position.bgn - ufpath[ii].position.end,
                 (int32)op[iid].max,
                 (int32)op[iid].min,
                 opll,
                 readLen,
                 opdd);

      ufpath[ii].position.bgn = (int32)op[iid].max;
      ufpath[ii].position.end = (int32)op[iid].min;
    }
  }
}



void
TigVector::optimizePositions(const char *prefix, const char *label) {
  uint32  numThreads  = omp_get_max_threads();

  uint32  tiLimit     = size();
  uint32  tiBlockSize = 10; //(tiLimit <   10 * numThreads) ? numThreads : tiLimit / 9;

  uint32  fiLimit     = RI->numReads() + 1;
  uint32  fiBlockSize = 100; //(fiLimit < 1000 * numThreads) ? numThreads : fiLimit / 999;

  bool    beVerbose   = false;

  writeStatus("optimizePositions()-- Optimizing read positions for %u reads in %u tigs, with %u thread%s.\n",
              fiLimit, tiLimit, numThreads, (numThreads == 1) ? "" : "s");

  //  Create work space and initialize to current read positions.

  writeStatus("optimizePositions()--   Allocating scratch space for %u reads (%u KB).\n", fiLimit, sizeof(optPos) * fiLimit * 2 >> 1024);

  optPos *pp = NULL;
  optPos *op = new optPos [fiLimit];
  optPos *np = new optPos [fiLimit];

  memset(op, 0, sizeof(optPos) * fiLimit);
  memset(np, 0, sizeof(optPos) * fiLimit);

  for (uint32 fi=0; fi<fiLimit; fi++) {
    uint32 ti = inUnitig(fi);
    uint32 pp = ufpathIdx(fi);

    if (ti == 0)
      continue;

    op[fi].set(operator[](ti)->ufpath[pp]);
    np[fi].set(operator[](ti)->ufpath[pp]);
  }

  //  Compute initial positions using previously placed reads and the read length.


  //
  //  Initialize positions using only reads before us.  If any reads fail to find overlaps, a second
  //  round will init positions using any read (before or after).
  //

  writeStatus("optimizePositions()--   Initializing positions with %u threads.\n", numThreads);

#pragma omp parallel for schedule(dynamic, tiBlockSize)
  for (uint32 ti=0; ti<tiLimit; ti++) {
    Unitig       *tig = operator[](ti);
    set<uint32>   failed;

    if (tig == NULL)
      continue;

    for (uint32 ii=0; ii<tig->ufpath.size(); ii++)
      tig->optimize_initPlace(ii, op, np, true,  failed, beVerbose);

    for (uint32 ii=0; ii<tig->ufpath.size(); ii++)
      tig->optimize_initPlace(ii, op, np, false, failed, true);
  }

  //
  //  Recompute positions using all overlaps and reads both before and after.  Do this for a handful of iterations
  //  so it somewhat stabilizes.
  //

  for (uint32 iter=0; iter<5; iter++) {

    //  Recompute positions

    writeStatus("optimizePositions()--   Recomputing positions, iteration %u, with %u threads.\n", iter+1, numThreads);

#pragma omp parallel for schedule(dynamic, fiBlockSize)
    for (uint32 fi=0; fi<fiLimit; fi++) {
      uint32 ti = inUnitig(fi);

      if (ti == 0)
        continue;

      operator[](ti)->optimize_recompute(fi, op, np, beVerbose);
    }

    //  Reset zero

    writeStatus("optimizePositions()--     Reset zero.\n");

    for (uint32 ti=0; ti<tiLimit; ti++) {
      Unitig       *tig = operator[](ti);

      if (tig == NULL)
        continue;

      int32  z = np[ tig->ufpath[0].ident ].min;

      for (uint32 ii=0; ii<tig->ufpath.size(); ii++) {
        uint32  iid = tig->ufpath[ii].ident;

        np[iid].min -= z;
        np[iid].max -= z;
      }
    }

    //  Decide if we've converged.  We used to compute percent difference in coordinates, but that is
    //  biased by the position of the read.  Just use percent difference from read length.

    writeStatus("optimizePositions()--     Checking convergence.\n");

    uint32  nConverged = 0;
    uint32  nChanged   = 0;

    for (uint32 fi=0; fi<fiLimit; fi++) {
      double  minp = 2 * (op[fi].min - np[fi].min) / (RI->readLength(fi));
      double  maxp = 2 * (op[fi].max - np[fi].max) / (RI->readLength(fi));

      if (minp < 0)  minp = -minp;
      if (maxp < 0)  maxp = -maxp;

      if ((minp < 0.005) && (maxp < 0.005))
        nConverged++;
      else
        nChanged++;
    }

    //  All reads processed, swap op and np for the next iteration.

    pp = op;
    op = np;
    np = pp;

    writeStatus("optimizePositions()--     converged: %6u reads\n", nConverged);
    writeStatus("optimizePositions()--     changed:   %6u reads\n", nChanged);

    if (nChanged == 0)
      break;
  }

  //
  //  Reset small reads.  If we've placed a read too small, expand it (and all reads that overlap)
  //  to make the length not smaller.
  //

  writeStatus("optimizePositions()--   Expanding short reads with %u threads.\n", numThreads);

#pragma omp parallel for schedule(dynamic, tiBlockSize)
  for (uint32 ti=0; ti<tiLimit; ti++) {
    Unitig       *tig = operator[](ti);

    if (tig == NULL)
      continue;

    tig->optimize_expand(op);
  }

  //
  //  Update the tig with new positions.  op[] is the result of the last iteration.
  //

  writeStatus("optimizePositions()--   Updating positions.\n");

  for (uint32 ti=0; ti<tiLimit; ti++) {
    Unitig       *tig = operator[](ti);

    if (tig == NULL)
      continue;

    tig->optimize_setPositions(op, beVerbose);
    tig->cleanUp();
  }

  //  Cleanup and finish.

  delete [] op;
  delete [] np;

  writeStatus("optimizePositions()--   Finished.\n");
}