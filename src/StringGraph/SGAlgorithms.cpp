//-----------------------------------------------
// Copyright 2009 Wellcome Trust Sanger Institute
// Written by Jared Simpson (js18@sanger.ac.uk)
// Released under the GPL
//-----------------------------------------------
//
// SGAlgorithms - Collection of algorithms for operating on string graphs
//
#include "SGAlgorithms.h"
#include "SGUtil.h"
#include "ErrorCorrect.h"
#include <iterator>

// Find new edges for pVertex that are required if pDeleteEdge is removed from the graph
// The algorithm is as follows. All the vertices that can be reached by vertices other
// than the deletion vertex are marked as reachable and not considered. The vertices
// uniquely reachable through the deletion vertex are considered in order of putative
// overlap with pVertex. If a new edge is created, all vertices reachable from that
// edge are subsequently marked as reachable so that no transitive edges are created.
void SGAlgorithms::remodelVertexForExcision(StringGraph* pGraph, Vertex* pVertex, Edge* pDeleteEdge)
{
    assert(pVertex == pDeleteEdge->getStart());

    // this is the initial overlap between pVertex and pDeletionVertex
    Overlap ovrXY = pDeleteEdge->getOverlap();
    EdgeDesc edXY = pDeleteEdge->getDesc();

    // Construct the set of vertices that are reachable by valid edges
    EdgeDescOverlapMap exclusionSet;
    exclusionSet.insert(std::make_pair(edXY, ovrXY));

    // Recursively add the vertices connected to pX to the exclusionSet
    // except for the neighbors that are exclusively reachable from pDeleteVertex
    EdgePtrVec edges = pVertex->getEdges();
    for(size_t i = 0; i < edges.size(); ++i)
    {
        Edge* pEdge = edges[i];
        
        // Skip adding vertices for the deletion edge
        if(pEdge != pDeleteEdge)
        {
            Overlap ovr = pEdge->getOverlap();
            EdgeDesc ed = pEdge->getDesc();
            exclusionSet.insert(std::make_pair(ed, ovr));

            // Recursively add the neighbors of pEnd to the set
            addOverlapsToSet(pVertex, ed, ovr, 1.0f, 0, exclusionSet);
        }
    }
    
    // Build the initial set of potential new overlaps from the 
    // neighbors of pDeleteVertex. Filter out any edges that are 
    // already present in the exclusion set. We don't want the exclusion
    // set to be modified

    EdgeDescSet seenEdges;
    // Populate the seen edges with the contents of the exclusion set
    for(EdgeDescOverlapMap::iterator iter = exclusionSet.begin(); 
                                     iter != exclusionSet.end(); ++iter)
    {
        seenEdges.insert(iter->first);
    }

    ExplorePriorityQueue exploreQueue;
    enqueueEdges(pVertex, edXY, ovrXY, exploreQueue, &seenEdges);

    // Iterate through the queue in order of overlap length
    while(!exploreQueue.empty())
    {
        ExploreElement currElem = exploreQueue.top();
        exploreQueue.pop();
        // Case 1, endpoint is reachable from some other edge of pVertex
        // and is therefore transitive
        if(exclusionSet.count(currElem.ed) > 0)
            continue;

        // Case 2, this may form a valid edge
        double error_rate = calcErrorRate(pVertex, currElem.ed.pVertex, currElem.ovr);
        int overlap_len = currElem.ovr.match.getMinOverlapLength();
        if(overlap_len >= pGraph->getMinOverlap())
        {
            if(isErrorRateAcceptable(error_rate, pGraph->getErrorRate()))
            {
                //std::cout << "Adding edge " << currElem.ovr << "\n";
                Edge* pCreatedEdge = SGUtil::createEdges(pGraph, currElem.ovr, false);
                assert(pCreatedEdge != NULL);
                assert(pCreatedEdge->getDesc() == currElem.ed);
                
                // This vertex is now connected to pVertex, add its neighbors to the exclusion set
                addOverlapsToSet(pVertex, currElem.ed, currElem.ovr, 1.0f, 0, exclusionSet);
            }
        }
    }
}

// Add the neighbors of pY to the explore queue if they overlap pX. If pSeenSet
// is not NULL and the edge is present in the set do not add neighbors of pY to the queue
void SGAlgorithms::enqueueEdges(const Vertex* pX, const EdgeDesc& edXY, const Overlap& ovrXY, 
                                ExplorePriorityQueue& outQueue, EdgeDescSet* pSeenSet)
{
    Vertex* pY = edXY.pVertex;
    EdgeDir dirY = correctDir(edXY.dir, edXY.comp);
    EdgePtrVec neighborEdges = pY->getEdges(dirY);

    for(size_t i = 0; i < neighborEdges.size(); ++i)
    {
        Edge* pEdgeYZ = neighborEdges[i];
        if(pEdgeYZ->getEnd() != pX)
        {
            EdgeDesc edYZ = pEdgeYZ->getDesc();
            EdgeDesc edXZ = SGAlgorithms::inferTransitiveEdgeDesc(edXY, edYZ);
            bool isExcluded = pSeenSet != NULL && pSeenSet->count(edXZ) > 0;
            if(!isExcluded)
            {
                Overlap ovrYZ = pEdgeYZ->getOverlap();
                
                // Check that this vertex actually overlaps pX
                if(SGAlgorithms::hasTransitiveOverlap(ovrXY, ovrYZ))
                {
                    Overlap ovrXZ = SGAlgorithms::inferTransitiveOverlap(ovrXY, ovrYZ);
                    //std::cout << "Inferred overlap: " << ovrXZ << " ed: " << edXZ << " from: " << pY->getID() << "\n";
                    ExploreElement elem(edXZ, ovrXZ);
                    outQueue.push(elem);
                    pSeenSet->insert(edXZ);
                    enqueueEdges(pX, edXZ, ovrXZ, outQueue, pSeenSet);
                }
            }
        }
    }
}

// Recursively add overlaps to pX inferred from the edges of pY to outMap
void SGAlgorithms::addOverlapsToSet(const Vertex* pX, const EdgeDesc& edXY, const Overlap& ovrXY, 
                                    double maxER, int minLength, EdgeDescOverlapMap& outMap)
{
    Vertex* pY = edXY.pVertex;

    // Calculate the direction of the edge coming out of Y
    EdgeDir dirY = correctDir(edXY.dir, edXY.comp);
    EdgePtrVec neighborEdges = pY->getEdges(dirY);
    for(size_t i = 0; i < neighborEdges.size(); ++i)
    {
        Edge* pEdgeYZ = neighborEdges[i];
        EdgeDesc edYZ = pEdgeYZ->getDesc();
        EdgeDesc edXZ = SGAlgorithms::inferTransitiveEdgeDesc(edXY, edYZ);
        Vertex* pZ = pEdgeYZ->getEnd();

        if(pZ != pX && outMap.count(edXZ) == 0)
        {
            Overlap ovrYZ = pEdgeYZ->getOverlap();
            EdgeDesc edYZ = pEdgeYZ->getDesc();

            // Check that this vertex actually overlaps pX
            if(SGAlgorithms::hasTransitiveOverlap(ovrXY, ovrYZ))
            {
                Overlap ovrXZ = SGAlgorithms::inferTransitiveOverlap(ovrXY, ovrYZ);
                EdgeDesc edXZ = SGAlgorithms::inferTransitiveEdgeDesc(edXY, edYZ);

                double error_rate = SGAlgorithms::calcErrorRate(pX, pZ, ovrXZ);
                if(isErrorRateAcceptable(error_rate, maxER) && ovrXZ.getOverlapLength(0) >= minLength)
                {
                    outMap.insert(std::make_pair(edXZ, ovrXZ));
                    addOverlapsToSet(pX, edXZ, ovrXZ, maxER, minLength, outMap);
                }
            }
        }
    }
}

// Calculate the error rate between the two vertices
double SGAlgorithms::calcErrorRate(const Vertex* pX, const Vertex* pY, const Overlap& ovrXY)
{
    int num_diffs = ovrXY.match.countDifferences(pX->getSeq(), pY->getSeq());
    return static_cast<double>(num_diffs) / static_cast<double>(ovrXY.match.getMinOverlapLength());
}

// Infer an overlap from two edges
// The input edges are between X->Y Y->Z
// and the returned overlap is X->Z
Overlap SGAlgorithms::inferTransitiveOverlap(const Overlap& ovrXY, const Overlap& ovrYZ)
{
    // Construct the match
    Match match_yx = ovrXY.match;
    match_yx.swap(); 
    Match match_yz = ovrYZ.match;

    // Infer the match_ij based match_i and match_j
    Match match_xz = Match::infer(match_yx, match_yz);
    match_xz.expand();

    // Convert the match to an overlap
    Overlap ovr(ovrXY.id[0], ovrYZ.id[1], match_xz);
    return ovr;
}

// Infer an EdgeDesc between X -> Z
// given EdgeDescs X -> Y, Y -> Z
// The input edges are between X->Y Y->Z
// and the returned overlap is X->Z
EdgeDesc SGAlgorithms::inferTransitiveEdgeDesc(const EdgeDesc& edXY, const EdgeDesc& edYZ)
{
    EdgeDesc out;
    out.pVertex = edYZ.pVertex; // the endpoint is Z
    out.dir = edXY.dir; // it must be in the same direction as X->Y
    out.comp = (edYZ.comp == EC_REVERSE) ? !edXY.comp : edXY.comp;
    return out;
}

// Return true if XZ has an overlap
bool SGAlgorithms::hasTransitiveOverlap(const Overlap& ovrXY, const Overlap& ovrYZ)
{
    Match match_yx = ovrXY.match;
    match_yx.swap(); 
    Match match_yz = ovrYZ.match;
    return Match::doMatchesIntersect(match_yx, match_yz);
}

// Construct an extended multioverlap for a vertex
MultiOverlap SGAlgorithms::makeExtendedMultiOverlap(const Vertex* pVertex)
{
    EdgeDescOverlapMap overlapMap;
    constructCompleteOverlapMap(pVertex, 1.0f, 0, overlapMap);

    MultiOverlap mo(pVertex->getID(), pVertex->getSeq());
    for(EdgeDescOverlapMap::const_iterator iter = overlapMap.begin();
        iter != overlapMap.end(); ++iter)
    {
        mo.add(iter->first.pVertex->getSeq(), iter->second);
    }
    return mo;
}

//
void SGAlgorithms::makeExtendedSeqTries(const Vertex* pVertex, double p_error, SeqTrie* pLeftTrie, SeqTrie* pRightTrie)
{
    double lp = log(p_error);
    EdgeDescOverlapMap overlapMap;
    constructCompleteOverlapMap(pVertex, 1.0f, 0, overlapMap);

    for(EdgeDescOverlapMap::const_iterator iter = overlapMap.begin();
        iter != overlapMap.end(); ++iter)
    {
        // Coord[0] of the match is wrt pVertex, coord[1] is the other read
        std::string overlapped = iter->second.match.coord[1].getSubstring(iter->first.pVertex->getSeq());
        if(iter->second.match.isRC())
            overlapped = reverseComplement(overlapped);

        if(iter->second.match.coord[0].isRightExtreme())
        {
            overlapped = reverse(overlapped);
            pRightTrie->insert(overlapped, lp);
        }
        else
        {
            assert(iter->second.match.coord[0].isLeftExtreme());
            pLeftTrie->insert(overlapped, lp);
        }
    }        
}


// Get the complete set of overlaps for the given vertex
void SGAlgorithms::constructCompleteOverlapMap(const Vertex* pVertex, double maxER, int minLength, EdgeDescOverlapMap& outMap)
{
    EdgePtrVec edges = pVertex->getEdges();

    // Add the primary overlaps to the map, and all the nodes reachable from the primaries
    for(size_t i = 0; i < edges.size(); ++i)
    {
        Edge* pEdge = edges[i];
        EdgeDesc ed = pEdge->getDesc();
        Overlap ovr = pEdge->getOverlap();
        outMap.insert(std::make_pair(ed, ovr));

        // Recursively add neighbors
        addOverlapsToSet(pVertex, ed, ovr, maxER, minLength, outMap);
    }
}

// Partition the complete overlap set of pVertex into irreducible and transitive edge sets
// This algorithm is exhaustive as it does not use the topology of the graph to determine
// transitivity but rather directly computes it using the overlaps and parameters passed in
// This way we can use this function to remodel the graph after error correction
void SGAlgorithms::constructPartitionedOverlapMap(const Vertex* pVertex, double maxER, int minLength, 
                                                  EdgeDescOverlapMap& irreducibleMap, 
                                                  EdgeDescOverlapMap& transitiveMap)
{
    // Construct the complete set of potential overlaps for this vertex
    SGAlgorithms::constructCompleteOverlapMap(pVertex, maxER, minLength, irreducibleMap);

    //std::cout << "Processing: " << pVertex->getID() << "\n";
    EDOPairQueue overlapQueue;
    for(EdgeDescOverlapMap::iterator iter = irreducibleMap.begin();
        iter != irreducibleMap.end(); ++iter)
    {
        overlapQueue.push(std::make_pair(iter->first, iter->second));
    }

    // Traverse the list of overlaps in order of length and move elements from
    // the irreducible map to the transitive map
    while(!overlapQueue.empty())
    {
        EdgeDescOverlapPair edoPair = overlapQueue.top();
        overlapQueue.pop();

        EdgeDesc& edXY = edoPair.first;
        Overlap& ovrXY = edoPair.second;

        //std::cout << "CurrIR: " << ovrXY << " len: " << ovrXY.getOverlapLength(0) << "\n";
        
        SGAlgorithms::EdgeDescOverlapMap::iterator iter = irreducibleMap.begin();
        while(iter != irreducibleMap.end())
        {
            bool move = false;
            const EdgeDesc& edXZ = iter->first;
            const Overlap& ovrXZ = iter->second;

            // Skip the self-match and any edges in the wrong direction
            if(!(edXZ == edXY) && edXY.dir == edXZ.dir && ovrXY.getOverlapLength(0) > ovrXZ.getOverlapLength(0))
            {
                // Infer the YZ overlap
                Overlap ovrYX = ovrXY;
                ovrYX.swap();
                Overlap ovrYZ = SGAlgorithms::inferTransitiveOverlap(ovrYX, ovrXZ);

                // Compute the error rate between the sequences
                double error_rate = SGAlgorithms::calcErrorRate(edXY.pVertex, edXZ.pVertex, ovrYZ);
                
                //std::cout << "\tOVRXY: " << ovrXY << "\n";
                //std::cout << "\tOVRXZ: " << ovrXZ << "\n";
                //std::cout << "\tOVRYZ: " << ovrYZ << " er: " << error_rate << "\n";
                
                if(isErrorRateAcceptable(error_rate, maxER) && 
                   ovrYZ.getOverlapLength(0) >= minLength)
                {
                    move = true;
                }
            }
            
            if(move)
            {
                transitiveMap.insert(*iter);
                irreducibleMap.erase(iter++);
            }
            else
            {
                ++iter;
            }
        }
    }
}

//
EdgeDesc SGAlgorithms::getEdgeDescFromEdge(Edge* pEdge)
{
    return pEdge->getDesc();
}

//
EdgeDesc SGAlgorithms::getEdgeDescFromPair(const EdgeDescOverlapPair& pair)
{
    return pair.first;
}


