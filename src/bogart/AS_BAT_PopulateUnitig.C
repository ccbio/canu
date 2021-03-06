
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
 *    src/AS_BAT/AS_BAT_PopulateUnitig.C
 *
 *  Modifications by:
 *
 *    Brian P. Walenz from 2010-NOV-23 to 2013-AUG-01
 *      are Copyright 2010-2013 J. Craig Venter Institute, and
 *      are subject to the GNU General Public License version 2
 *
 *    Brian P. Walenz on 2014-DEC-19
 *      are Copyright 2014 Battelle National Biodefense Institute, and
 *      are subject to the BSD 3-Clause License
 *
 *    Brian P. Walenz beginning on 2016-JAN-11
 *      are a 'United States Government Work', and
 *      are released in the public domain
 *
 *  File 'README.licenses' in the root directory of this distribution contains
 *  full conditions and disclaimers for each license.
 */

#include "AS_BAT_ReadInfo.H"
#include "AS_BAT_BestOverlapGraph.H"
#include "AS_BAT_Logging.H"

#include "AS_BAT_Unitig.H"

#include "AS_BAT_PopulateUnitig.H"


void
populateUnitig(Unitig           *unitig,
               BestEdgeOverlap  *bestnext) {

  assert(unitig->getLength() > 0);

  if ((bestnext == NULL) || (bestnext->readId() == 0))
    //  Nothing to add!
    return;

  ufNode  read    = unitig->ufpath.back();

  //  The ID of the last read in the unitig, and the end we should walk off of it.
  int32   lastID  = read.ident;
  bool    last3p  = (read.position.bgn < read.position.end);

  uint32  nAdded  = 0;

  //  While there are reads to add AND those reads to add are not already in a unitig,
  //  construct a reverse-edge, and add the read.

  while ((bestnext->readId() != 0) &&
         (unitig->inUnitig(bestnext->readId()) == 0)) {
    BestEdgeOverlap  bestprev;

    //  Reverse nextedge (points from the unitig to the next read to add) so that it points from
    //  the next read to add back to something in the unitig.  If the reads are
    //  innie/outtie, we need to reverse the overlap to maintain that the A read is forward.

    if (last3p == bestnext->read3p())
      bestprev.set(lastID, last3p, bestnext->bhang(), bestnext->ahang(), bestnext->evalue());
    else
      bestprev.set(lastID, last3p, -bestnext->ahang(), -bestnext->bhang(), bestnext->evalue());

    //  We just made 'bestprev' pointing from read 'bestnext->readId()' end 'bestnext->read3p()'
    //  back to read 'lastID' end 'last3p'.  Compute the placement.

    if (unitig->placeRead(read, bestnext->readId(), bestnext->read3p(), &bestprev)) {
      unitig->addRead(read, 0, false);
      nAdded++;

    } else {
      writeLog("ERROR:  Failed to place read %d into BOG path.\n", read.ident);
      assert(0);
    }

    //  Set up for the next read

    lastID  = read.ident;
    last3p  = (read.position.bgn < read.position.end);

    bestnext = OG->getBestEdgeOverlap(lastID, last3p);
  }

  if (logFileFlagSet(LOG_BUILD_UNITIG))
    if (bestnext->readId() == 0)
      writeLog("Stopped adding at read %u/%c' because no next best edge.  Added %u reads.\n",
               lastID, (last3p) ? '3' : '5',
               nAdded);
    else
      writeLog("Stopped adding at read %u/%c' beacuse next best read %u/%c' is in unitig %u.  Added %u reads.\n",
               lastID, (last3p) ? '3' : '5',
               bestnext->readId(), bestnext->read3p() ? '3' : '5',
               unitig->inUnitig(bestnext->readId()),
               nAdded);
}




void
populateUnitig(TigVector &tigs,
               int32      fi) {

  if ((RI->readLength(fi) == 0) ||      //  Skip deleted
      (tigs.inUnitig(fi) != 0))         //  Skip placed
    return;

  if ((OG->isContained(fi) == true) &&  //  Skip contained...
      (OG->isZombie(fi) == false))      //  that aren't zombies.
    return;

  Unitig *utg = tigs.newUnitig(logFileFlagSet(LOG_BUILD_UNITIG));

  //  Add a first read -- to be 'compatable' with the old code, the first read is added
  //  reversed, we walk off of its 5' end, flip it, and add the 3' walk.

  ufNode  read;

  read.ident             = fi;
  read.contained         = 0;
  read.parent            = 0;
  read.ahang             = 0;
  read.bhang             = 0;
  read.position.bgn      = RI->readLength(fi);
  read.position.end      = 0;

  utg->addRead(read, 0, logFileFlagSet(LOG_BUILD_UNITIG));

  //  If suspicious or a zombie, don't bother trying to extend.  In the former
  //  case, we don't want to extend, and in the latter case, there isn't anything
  //  to extend.

  if (OG->isSuspicious(fi)) {
    writeLog("Stopping unitig construction of suspicious read %d in unitig %d\n",
            utg->ufpath.back().ident, utg->id());
    return;
  }

  if (OG->isZombie(fi)) {
    writeLog("Stopping unitig construction of zombie read %d in unitig %d\n",
            utg->ufpath.back().ident, utg->id());
    return;
  }

  //  Add reads as long as there is a path to follow...from the 3' end of the first read.

  BestEdgeOverlap  *bestedge5 = OG->getBestEdgeOverlap(fi, false);
  BestEdgeOverlap  *bestedge3 = OG->getBestEdgeOverlap(fi, true);

  assert(bestedge5->ahang() <= 0);  //  Best Edges must be dovetail, which makes this test
  assert(bestedge5->bhang() <= 0);  //  much simpler.
  assert(bestedge3->ahang() >= 0);
  assert(bestedge3->bhang() >= 0);

  if (logFileFlagSet(LOG_BUILD_UNITIG))
    writeLog("Adding 5' edges off of read %d in unitig %d\n",
            utg->ufpath.back().ident, utg->id());

  if (bestedge5->readId())
    populateUnitig(utg, bestedge5);

  utg->reverseComplement(false);

  if (logFileFlagSet(LOG_BUILD_UNITIG))
    writeLog("Adding 3' edges off of read %d in unitig %d\n",
            utg->ufpath.back().ident, utg->id());

  if (bestedge3->readId())
    populateUnitig(utg, bestedge3);

  //  Enabling this reverse complement is known to degrade the assembly.  It is not known WHY it
  //  degrades the assembly.
  //
  //utg->reverseComplement(false);
}
