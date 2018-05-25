//-----------------------------------------------------------------------------
//   flowrout.c
//
//   Project:  EPA SWMM5
//   Version:  5.1
//   Date:     03/19/14  (Build 5.1.001)
//             09/15/14  (Build 5.1.007)
//             03/19/15  (Build 5.1.008)
//             03/14/11  (Build 5.1.012)
//   Author:   L. Rossman (EPA)
//             M. Tryby (EPA)
//
//   Flow routing functions.
//
//
//   Build 5.1.007:
//   - updateStorageState() modified in response to node outflow being 
//     initialized with current evap & seepage losses in routing_execute().
//
//   Build 5.1.008:
//   - Determination of node crown elevations moved to dynwave.c.
//   - Support added for new way of recording conduit's fullness state.
//
//   Build 5.1.012:
//   - Overflow computed in updateStorageState() must be non-negative.
//   - Terminal storage nodes now updated corectly.
//
//-----------------------------------------------------------------------------
#define _CRT_SECURE_NO_DEPRECATE

#include "headers.h"
#include <stdlib.h>
#include <math.h>

//-----------------------------------------------------------------------------
//  Constants
//-----------------------------------------------------------------------------
static const double OMEGA   = 0.55;    // under-relaxation parameter
static const int    MAXITER = 10;      // max. iterations for storage updating
static const double STOPTOL = 0.005;   // storage updating stopping tolerance

//-----------------------------------------------------------------------------
//  External functions (declared in funcs.h)
//-----------------------------------------------------------------------------
//  flowrout_init            (called by routing_open)
//  flowrout_close           (called by routing_close)
//  flowrout_getRoutingStep  (called routing_getRoutingStep)
//  flowrout_execute         (called routing_execute)

//-----------------------------------------------------------------------------
//  Local functions
//-----------------------------------------------------------------------------
static void   initLinkDepths(SWMM_Project *sp);
static void   initNodeDepths(SWMM_Project *sp);
static void   initNodes(SWMM_Project *sp);
static void   initLinks(SWMM_Project *sp, int routingModel);                                     //(5.1.008)
static void   validateTreeLayout(SWMM_Project *sp);
static void   validateGeneralLayout(SWMM_Project *sp);
static void   updateStorageState(SWMM_Project *sp, int i, int j, int links[], double dt);
static double getStorageOutflow(SWMM_Project *sp, int node, int j, int links[], double dt);
static double getLinkInflow(SWMM_Project *sp, int link, double dt);
static void   setNewNodeState(SWMM_Project *sp, int node, double dt);
static void   setNewLinkState(SWMM_Project *sp, int link);
static void   updateNodeDepth(SWMM_Project *sp, int node, double y);
static int    steadyflow_execute(SWMM_Project *sp, int link, double* qin,
        double* qout, double tStep);


//=============================================================================

void flowrout_init(SWMM_Project *sp, int routingModel)
//
//  Input:   routingModel = routing model code
//  Output:  none
//  Purpose: initializes flow routing system.
//
{
    // --- initialize for dynamic wave routing 
    if ( routingModel == DW )
    {
        // --- check for valid conveyance network layout
        validateGeneralLayout(sp);
        dynwave_init(sp);

        // --- initialize node & link depths if not using a hotstart file
        if ( sp->Fhotstart1.mode == NO_FILE )
        {
            initNodeDepths(sp);
            initLinkDepths(sp);
        }
    }

    // --- validate network layout for kinematic wave routing
    else validateTreeLayout(sp);

    // --- initialize node & link volumes
    initNodes(sp);
    initLinks(sp, routingModel);                                                   //(5.1.008)
}

//=============================================================================

void  flowrout_close(SWMM_Project *sp, int routingModel)
//
//  Input:   routingModel = routing method code
//  Output:  none
//  Purpose: closes down routing method used.
//
{
    if ( routingModel == DW ) dynwave_close(sp);
}

//=============================================================================

double flowrout_getRoutingStep(SWMM_Project *sp, int routingModel, double fixedStep)
//
//  Input:   routingModel = type of routing method used
//           fixedStep = user-assigned max. routing step (sec)
//  Output:  returns adjusted value of routing time step (sec)
//  Purpose: finds variable time step for dynamic wave routing.
//
{
    if ( routingModel == DW )
    {
        return dynwave_getRoutingStep(sp, fixedStep);
    }
    return fixedStep;
}

//=============================================================================

int flowrout_execute(SWMM_Project *sp, int links[], int routingModel, double tStep)
//
//  Input:   links = array of link indexes in topo-sorted order
//           routingModel = type of routing method used
//           tStep = routing time step (sec)
//  Output:  returns number of computational steps taken
//  Purpose: routes flow through conveyance network over current time step.
//
{
    int   i, j;
    int   n1;                          // upstream node of link
    double qin;                        // link inflow (cfs)
    double qout;                       // link outflow (cfs)
    double steps;                      // computational step count

    // --- set overflows to drain any ponded water
    if ( sp->ErrorCode ) return 0;
    for (j = 0; j < sp->Nobjects[NODE]; j++)
    {
        sp->Node[j].updated = FALSE;
        sp->Node[j].overflow = 0.0;
        if ( sp->Node[j].type != STORAGE
        &&   sp->Node[j].newVolume > sp->Node[j].fullVolume )
        {
            sp->Node[j].overflow = (sp->Node[j].newVolume - sp->Node[j].fullVolume)/tStep;
        }
    }

    // --- execute dynamic wave routing if called for
    if ( routingModel == DW )
    {
        return dynwave_execute(sp, tStep);
    }

    // --- otherwise examine each link, moving from upstream to downstream
    steps = 0.0;
    for (i = 0; i < sp->Nobjects[LINK]; i++)
    {
        // --- see if upstream node is a storage unit whose state needs updating
        j = links[i];
        n1 = sp->Link[j].node1;
        if ( sp->Node[n1].type == STORAGE ) updateStorageState(sp, n1, i, links, tStep);

        // --- retrieve inflow at upstream end of link
        qin  = getLinkInflow(sp, j, tStep);

        // route flow through link
        if ( routingModel == SF )
            steps += steadyflow_execute(sp, j, &qin, &qout, tStep);
        else steps += kinwave_execute(sp, j, &qin, &qout, tStep);
        sp->Link[j].newFlow = qout;

        // adjust outflow at upstream node and inflow at downstream node
        sp->Node[ sp->Link[j].node1 ].outflow += qin;
        sp->Node[ sp->Link[j].node2 ].inflow += qout;
    }
    if ( sp->Nobjects[LINK] > 0 ) steps /= sp->Nobjects[LINK];

    // --- update state of each non-updated node and link
    for ( j=0; j<sp->Nobjects[NODE]; j++) setNewNodeState(sp, j, tStep);
    for ( j=0; j<sp->Nobjects[LINK]; j++) setNewLinkState(sp, j);
    return (int)(steps+0.5);
}

//=============================================================================

void validateTreeLayout(SWMM_Project *sp)
//
//  Input:   none
//  Output:  none
//  Purpose: validates tree-like conveyance system layout used for Steady
//           and Kinematic Wave flow routing
//
{
    int    j;

    // --- check nodes
    for ( j = 0; j < sp->Nobjects[NODE]; j++ )
    {
        switch ( sp->Node[j].type )
        {
          // --- dividers must have only 2 outlet links
          case DIVIDER:
            if ( sp->Node[j].degree > 2 )
            {
                report_writeErrorMsg(sp, ERR_DIVIDER, sp->Node[j].ID);
            }
            break;

          // --- outfalls cannot have any outlet links
          case OUTFALL:
            if ( sp->Node[j].degree > 0 )
            {
                report_writeErrorMsg(sp, ERR_OUTFALL, sp->Node[j].ID);
            }
            break;

          // --- storage nodes can have multiple outlets
          case STORAGE: break;

          // --- all other nodes allowed only one outlet link
          default:
            if ( sp->Node[j].degree > 1 )
            {
                report_writeErrorMsg(sp, ERR_MULTI_OUTLET, sp->Node[j].ID);
            }
        }
    }

    // ---  check links 
    for (j=0; j<sp->Nobjects[LINK]; j++)
    {
        switch ( sp->Link[j].type )
        {
          // --- non-dummy conduits cannot have adverse slope
          case CONDUIT:
              if ( sp->Conduit[sp->Link[j].subIndex].slope < 0.0 &&
                   sp->Link[j].xsect.type != DUMMY )
              {
                  report_writeErrorMsg(sp, ERR_SLOPE, sp->Link[j].ID);
              }
              break;

          // --- regulator links must be outlets of storage nodes
          case ORIFICE:
          case WEIR:
          case OUTLET:
            if ( sp->Node[sp->Link[j].node1].type != STORAGE )
            {
                report_writeErrorMsg(sp, ERR_REGULATOR, sp->Link[j].ID);
            }
        }
    }
}

//=============================================================================

void validateGeneralLayout(SWMM_Project *sp)
//
//  Input:   none
//  Output:  nonw
//  Purpose: validates general conveyance system layout.
//
{
    int i, j;
    int outletCount = 0;

    // --- use node inflow attribute to count inflow connections
    for ( i=0; i<sp->Nobjects[NODE]; i++ ) sp->Node[i].inflow = 0.0;

    // --- examine each link
    for ( j = 0; j < sp->Nobjects[LINK]; j++ )
    {
        // --- update inflow link count of downstream node
        i = sp->Link[j].node1;
        if ( sp->Node[i].type != OUTFALL ) i = sp->Link[j].node2;
        sp->Node[i].inflow += 1.0;

        // --- if link is dummy link or ideal pump then it must
        //     be the only link exiting the upstream node 
        if ( (sp->Link[j].type == CONDUIT && sp->Link[j].xsect.type == DUMMY) ||
             (sp->Link[j].type == PUMP &&
              sp->Pump[sp->Link[j].subIndex].type == IDEAL_PUMP) )
        {
            i = sp->Link[j].node1;
            if ( sp->Link[j].direction < 0 ) i = sp->Link[j].node2;
            if ( sp->Node[i].degree > 1 )
            {
                report_writeErrorMsg(sp, ERR_DUMMY_LINK, sp->Node[i].ID);
            }
        }
    }

    // --- check each node to see if it qualifies as an outlet node
    //     (meaning that degree = 0)
    for ( i = 0; i < sp->Nobjects[NODE]; i++ )
    {
        // --- if node is of type Outfall, check that it has only 1
        //     connecting link (which can either be an outflow or inflow link)
        if ( sp->Node[i].type == OUTFALL )
        {
            if ( sp->Node[i].degree + (int)sp->Node[i].inflow > 1 )
            {
                report_writeErrorMsg(sp, ERR_OUTFALL, sp->Node[i].ID);
            }
            else outletCount++;
        }
    }
    if ( outletCount == 0 ) report_writeErrorMsg(sp, ERR_NO_OUTLETS, "");

    // --- reset node inflows back to zero
    for ( i = 0; i < sp->Nobjects[NODE]; i++ )
    {
        if ( sp->Node[i].inflow == 0.0 ) sp->Node[i].degree = -sp->Node[i].degree;
        sp->Node[i].inflow = 0.0;
    }
}

//=============================================================================

void initNodeDepths(SWMM_Project *sp)
//
//  Input:   none
//  Output:  none
//  Purpose: sets initial depth at nodes for Dynamic Wave flow routing.
//
{
    int   i;                           // link or node index
    int   n;                           // node index
    double y;                          // node water depth (ft)

    // --- use sp->Node[].inflow as a temporary accumulator for depth in 
    //     connecting links and sp->Node[].outflow as a temporary counter
    //     for the number of connecting links
    for (i = 0; i < sp->Nobjects[NODE]; i++)
    {
        sp->Node[i].inflow  = 0.0;
        sp->Node[i].outflow = 0.0;
    }

    // --- total up flow depths in all connecting links into nodes
    for (i = 0; i < sp->Nobjects[LINK]; i++)
    {
        if ( sp->Link[i].newDepth > FUDGE ) y = sp->Link[i].newDepth + sp->Link[i].offset1;
        else y = 0.0;
        n = sp->Link[i].node1;
        sp->Node[n].inflow += y;
        sp->Node[n].outflow += 1.0;
        n = sp->Link[i].node2;
        sp->Node[n].inflow += y;
        sp->Node[n].outflow += 1.0;
    }

    // --- if no user-supplied depth then set initial depth at non-storage/
    //     non-outfall nodes to average of depths in connecting links
    for ( i = 0; i < sp->Nobjects[NODE]; i++ )
    {
        if ( sp->Node[i].type == OUTFALL ) continue;
        if ( sp->Node[i].type == STORAGE ) continue;
        if ( sp->Node[i].initDepth > 0.0 ) continue;
        if ( sp->Node[i].outflow > 0.0 )
        {
            sp->Node[i].newDepth = sp->Node[i].inflow / sp->Node[i].outflow;
        }
    }

    // --- compute initial depths at all outfall nodes
    for ( i = 0; i < sp->Nobjects[LINK]; i++ ) link_setOutfallDepth(sp, i);
}

//=============================================================================
         
void initLinkDepths(SWMM_Project *sp)
//
//  Input:   none
//  Output:  none
//  Purpose: sets initial flow depths in conduits under Dyn. Wave routing.
//
{
    int    i;                          // link index
    double y, y1, y2;                  // depths (ft)

    // --- examine each link
    for (i = 0; i < sp->Nobjects[LINK]; i++)
    {
        // --- examine each conduit
        if ( sp->Link[i].type == CONDUIT )
        {
            // --- skip conduits with user-assigned initial flows
            //     (their depths have already been set to normal depth)
            if ( sp->Link[i].q0 != 0.0 ) continue;

            // --- set depth to average of depths at end nodes
            y1 = sp->Node[sp->Link[i].node1].newDepth - sp->Link[i].offset1;
            y1 = MAX(y1, 0.0);
            y1 = MIN(y1, sp->Link[i].xsect.yFull);
            y2 = sp->Node[sp->Link[i].node2].newDepth - sp->Link[i].offset2;
            y2 = MAX(y2, 0.0);
            y2 = MIN(y2, sp->Link[i].xsect.yFull);
            y = 0.5 * (y1 + y2);
            y = MAX(y, FUDGE);
            sp->Link[i].newDepth = y;
        }
    }
}

//=============================================================================

////  This function was modified for release 5.1.008.  ////                    //(5.1.008)

void initNodes(SWMM_Project *sp)
//
//  Input:   none
//  Output:  none
//  Purpose: sets initial inflow/outflow and volume for each node
//
{
    int i;

    for ( i = 0; i < sp->Nobjects[NODE]; i++ )
    {
        // --- initialize node inflow and outflow
        sp->Node[i].inflow = sp->Node[i].newLatFlow;
        sp->Node[i].outflow = 0.0;

        // --- initialize node volume
        sp->Node[i].newVolume = 0.0;
        if ( sp->AllowPonding &&
             sp->Node[i].pondedArea > 0.0 &&
             sp->Node[i].newDepth > sp->Node[i].fullDepth )
        {
            sp->Node[i].newVolume = sp->Node[i].fullVolume +
                                (sp->Node[i].newDepth - sp->Node[i].fullDepth) *
                                sp->Node[i].pondedArea;
        }
        else sp->Node[i].newVolume = node_getVolume(sp, i, sp->Node[i].newDepth);
    }

    // --- update nodal inflow/outflow at ends of each link
    //     (needed for Steady Flow & Kin. Wave routing)
    for ( i = 0; i < sp->Nobjects[LINK]; i++ )
    {
        if ( sp->Link[i].newFlow >= 0.0 )
        {
            sp->Node[sp->Link[i].node1].outflow += sp->Link[i].newFlow;
            sp->Node[sp->Link[i].node2].inflow  += sp->Link[i].newFlow;
        }
        else
        {
            sp->Node[sp->Link[i].node1].inflow   -= sp->Link[i].newFlow;
            sp->Node[sp->Link[i].node2].outflow  -= sp->Link[i].newFlow;
        }
    }
}

//=============================================================================

////  This function was modified for release 5.1.008.  ////                    //(5.1.008)

void initLinks(SWMM_Project *sp, int routingModel)
//
//  Input:   none
//  Output:  none
//  Purpose: sets initial upstream/downstream conditions in links.
//
{
    int    i;                          // link index
    int    k;                          // conduit or pump index

    // --- examine each link
    for ( i = 0; i < sp->Nobjects[LINK]; i++ )
    {
        if ( routingModel == SF) sp->Link[i].newFlow = 0.0;

        // --- otherwise if link is a conduit
        else if ( sp->Link[i].type == CONDUIT )
        {
            // --- assign initial flow to both ends of conduit
            k = sp->Link[i].subIndex;
            sp->Conduit[k].q1 = sp->Link[i].newFlow / sp->Conduit[k].barrels;
            sp->Conduit[k].q2 = sp->Conduit[k].q1;

            // --- find areas based on initial flow depth
            sp->Conduit[k].a1 = xsect_getAofY(sp, &sp->Link[i].xsect, sp->Link[i].newDepth);
            sp->Conduit[k].a2 = sp->Conduit[k].a1;

            // --- compute initial volume from area
            {
                sp->Link[i].newVolume = sp->Conduit[k].a1 * link_getLength(sp, i) *
                                    sp->Conduit[k].barrels;
            }
            sp->Link[i].oldVolume = sp->Link[i].newVolume;
        }
    }
}

//=============================================================================

double getLinkInflow(SWMM_Project *sp, int j, double dt)
//
//  Input:   j  = link index
//           dt = routing time step (sec)
//  Output:  returns link inflow (cfs)
//  Purpose: finds flow into upstream end of link at current time step under
//           Steady or Kin. Wave routing.
//
{
    int   n1 = sp->Link[j].node1;
    double q;
    if ( sp->Link[j].type == CONDUIT ||
         sp->Link[j].type == PUMP ||
         sp->Node[n1].type == STORAGE ) q = link_getInflow(sp, j);
    else q = 0.0;
    return node_getMaxOutflow(sp, n1, q, dt);
}

//=============================================================================

void updateStorageState(SWMM_Project *sp, int i, int j, int links[], double dt)
//
//  Input:   i = index of storage node
//           j = current position in links array
//           links = array of topo-sorted link indexes
//           dt = routing time step (sec)
//  Output:  none
//  Purpose: updates depth and volume of a storage node using successive
//           approximation with under-relaxation for Steady or Kin. Wave
//           routing.
//
{
    int    iter;                       // iteration counter
    int    stopped;                    // TRUE when iterations stop
    double vFixed;                     // fixed terms of flow balance eqn.
    double v2;                         // new volume estimate (ft3)
    double d1;                         // initial value of storage depth (ft)
    double d2;                         // updated value of storage depth (ft)

    // --- see if storage node needs updating
    if ( sp->Node[i].type != STORAGE ) return;
    if ( sp->Node[i].updated ) return;

    // --- compute terms of flow balance eqn.
    //       v2 = v1 + (inflow - outflow)*dt
    //     that do not depend on storage depth at end of time step
    vFixed = sp->Node[i].oldVolume + 
             0.5 * (sp->Node[i].oldNetInflow + sp->Node[i].inflow - 
                    sp->Node[i].outflow) * dt;                                     //(5.1.007)
    d1 = sp->Node[i].newDepth;

    // --- iterate finding outflow (which depends on depth) and subsequent
    //     new volume and depth until negligible depth change occurs
    iter = 1;
    stopped = FALSE;
    while ( iter < MAXITER && !stopped )
    {
        // --- find new volume from flow balance eqn.
        v2 = vFixed - 0.5 * getStorageOutflow(sp, i, j, links, dt) * dt;           //(5.1.007)

        // --- limit volume to full volume if no ponding
        //     and compute overflow rate
        v2 = MAX(0.0, v2);
        sp->Node[i].overflow = 0.0;
        if ( v2 > sp->Node[i].fullVolume )
        {
            sp->Node[i].overflow = (v2 - MAX(sp->Node[i].oldVolume,
                                         sp->Node[i].fullVolume)) / dt;
            if ( sp->Node[i].overflow < FUDGE ) sp->Node[i].overflow = 0.0;            //(5.1.012)
            if ( !sp->AllowPonding || sp->Node[i].pondedArea == 0.0 )
                v2 = sp->Node[i].fullVolume;
        }

        // --- update node's volume & depth 
        sp->Node[i].newVolume = v2;
        d2 = node_getDepth(sp, i, v2);
        sp->Node[i].newDepth = d2;

        // --- use under-relaxation to estimate new depth value
        //     and stop if close enough to previous value
        d2 = (1.0 - OMEGA)*d1 + OMEGA*d2;
        if ( fabs(d2 - d1) <= STOPTOL ) stopped = TRUE;

        // --- update old depth with new value and continue to iterate
        sp->Node[i].newDepth = d2;
        d1 = d2;
        iter++;
    }

    // --- mark node as being updated
    sp->Node[i].updated = TRUE;
}

//=============================================================================

double getStorageOutflow(SWMM_Project *sp, int i, int j, int links[], double dt)
//
//  Input:   i = index of storage node
//           j = current position in links array
//           links = array of topo-sorted link indexes
//           dt = routing time step (sec)
//  Output:  returns total outflow from storage node (cfs)
//  Purpose: computes total flow released from a storage node.
//
{
    int   k, m;
    double outflow = 0.0;

    for (k = j; k < sp->Nobjects[LINK]; k++)
    {
        m = links[k];
        if ( sp->Link[m].node1 != i ) break;
        outflow += getLinkInflow(sp, m, dt);
    }
    return outflow;        
}

//=============================================================================

void setNewNodeState(SWMM_Project *sp, int j, double dt)
//
//  Input:   j  = node index
//           dt = time step (sec)
//  Output:  none
//  Purpose: updates state of node after current time step
//           for Steady Flow or Kinematic Wave flow routing.
//
{
    int   canPond;                     // TRUE if ponding can occur at node  
    double newNetInflow;               // inflow - outflow at node (cfs)

////  Following section revised for release 5.1.012.  ////                     //(5.1.012)
    // --- update terminal storage nodes
    if ( sp->Node[j].type == STORAGE )
    {	
	if ( sp->Node[j].updated == FALSE )
	    updateStorageState(sp, j, sp->Nobjects[LINK], NULL, dt);
        return; 
    }
//////////////////////////////////////////////////////////

    // --- update stored volume using mid-point integration
    newNetInflow = sp->Node[j].inflow - sp->Node[j].outflow - sp->Node[j].losses;          //(5.1.007)
    sp->Node[j].newVolume = sp->Node[j].oldVolume +
                        0.5 * (sp->Node[j].oldNetInflow + newNetInflow) * dt;
    if ( sp->Node[j].newVolume < FUDGE ) sp->Node[j].newVolume = 0.0;

    // --- determine any overflow lost from system
    sp->Node[j].overflow = 0.0;
    canPond = (sp->AllowPonding && sp->Node[j].pondedArea > 0.0);
    if ( sp->Node[j].newVolume > sp->Node[j].fullVolume )
    {
        sp->Node[j].overflow = (sp->Node[j].newVolume - MAX(sp->Node[j].oldVolume,
                            sp->Node[j].fullVolume)) / dt;
        if ( sp->Node[j].overflow < FUDGE ) sp->Node[j].overflow = 0.0;
        if ( !canPond ) sp->Node[j].newVolume = sp->Node[j].fullVolume;
    }

    // --- compute a depth from volume
    //     (depths at upstream nodes are subsequently adjusted in
    //     setNewLinkState to reflect depths in connected conduit)
    sp->Node[j].newDepth = node_getDepth(sp, j, sp->Node[j].newVolume);
}

//=============================================================================

void setNewLinkState(SWMM_Project *sp, int j)
//
//  Input:   j = link index
//  Output:  none
//  Purpose: updates state of link after current time step under
//           Steady Flow or Kinematic Wave flow routing
//
{
    int   k;
    double a, y1, y2;

    sp->Link[j].newDepth = 0.0;
    sp->Link[j].newVolume = 0.0;

    if ( sp->Link[j].type == CONDUIT )
    {
        // --- find avg. depth from entry/exit conditions
        k = sp->Link[j].subIndex;
        a = 0.5 * (sp->Conduit[k].a1 + sp->Conduit[k].a2);
        sp->Link[j].newVolume = a * link_getLength(sp, j) * sp->Conduit[k].barrels;
        y1 = xsect_getYofA(sp, &sp->Link[j].xsect, sp->Conduit[k].a1);
        y2 = xsect_getYofA(sp, &sp->Link[j].xsect, sp->Conduit[k].a2);
        sp->Link[j].newDepth = 0.5 * (y1 + y2);

        // --- update depths at end nodes
        updateNodeDepth(sp, sp->Link[j].node1, y1 + sp->Link[j].offset1);
        updateNodeDepth(sp, sp->Link[j].node2, y2 + sp->Link[j].offset2);

        // --- check if capacity limited
        if ( sp->Conduit[k].a1 >= sp->Link[j].xsect.aFull )
        {
             sp->Conduit[k].capacityLimited = TRUE;
             sp->Conduit[k].fullState = ALL_FULL;                                  //(5.1.008)
        }
        else
        {    
            sp->Conduit[k].capacityLimited = FALSE;
            sp->Conduit[k].fullState = 0;                                          //(5.1.008)
        }
    }
}

//=============================================================================

void updateNodeDepth(SWMM_Project *sp, int i, double y)
//
//  Input:   i = node index
//           y = flow depth (ft)
//  Output:  none
//  Purpose: updates water depth at a node with a possibly higher value.
//
{
    // --- storage nodes were updated elsewhere
    if ( sp->Node[i].type == STORAGE ) return;

    // --- if non-outfall node is flooded, then use full depth
    if ( sp->Node[i].type != OUTFALL &&
         sp->Node[i].overflow > 0.0 ) y = sp->Node[i].fullDepth;

    // --- if current new depth below y
    if ( sp->Node[i].newDepth < y )
    {
        // --- update new depth
        sp->Node[i].newDepth = y;

        // --- depth cannot exceed full depth (if value exists)
        if ( sp->Node[i].fullDepth > 0.0 && y > sp->Node[i].fullDepth )
        {
            sp->Node[i].newDepth = sp->Node[i].fullDepth;
        }
    }
}

//=============================================================================

int steadyflow_execute(SWMM_Project *sp, int j, double* qin, double* qout, double tStep)
//
//  Input:   j = link index
//           qin = inflow to link (cfs)
//           tStep = time step (sec)
//  Output:  qin = adjusted inflow to link (limited by flow capacity) (cfs)
//           qout = link's outflow (cfs)
//           returns 1 if successful
//  Purpose: performs steady flow routing through a single link.
//
{
    int   k;
    double s;
    double q;

    // --- use Manning eqn. to compute flow area for conduits
    if ( sp->Link[j].type == CONDUIT )
    {
        k = sp->Link[j].subIndex;
        q = (*qin) / sp->Conduit[k].barrels;
        if ( sp->Link[j].xsect.type == DUMMY ) sp->Conduit[k].a1 = 0.0;
        else 
        {
            // --- subtract evap and infil losses from inflow
            q -= link_getLossRate(sp, j, q, tStep);                                //(5.1.008)
            if ( q < 0.0 ) q = 0.0;

            // --- flow can't exceed full flow 
            if ( q > sp->Link[j].qFull )
            {
                q = sp->Link[j].qFull;
                sp->Conduit[k].a1 = sp->Link[j].xsect.aFull;
                (*qin) = q * sp->Conduit[k].barrels;
            }

            // --- infer flow area from flow rate 
            else
            {
                s = q / sp->Conduit[k].beta;
                sp->Conduit[k].a1 = xsect_getAofS(sp, &sp->Link[j].xsect, s);
            }
        }
        sp->Conduit[k].a2 = sp->Conduit[k].a1;

        sp->Conduit[k].q1Old = sp->Conduit[k].q1;
        sp->Conduit[k].q2Old = sp->Conduit[k].q2;


        sp->Conduit[k].q1 = q;
        sp->Conduit[k].q2 = q;
        (*qout) = q * sp->Conduit[k].barrels;
    }
    else (*qout) = (*qin);
    return 1;
}

//=============================================================================