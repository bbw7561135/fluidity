/*  Copyright (C) 2006 Imperial College London and others.
    
    Please see the AUTHORS file in the main source directory for a full list
    of copyright holders.

    Prof. C Pain
    Applied Modelling and Computation Group
    Department of Earth Science and Engineering
    Imperial College London

    amcgsoftware@imperial.ac.uk
    
    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation,
    version 2.1 of the License.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307
    USA
*/
#include "fmangle.h"

#ifdef HAVE_LIBGMSH
#include "gmsh/Gmsh.h"
#include "gmsh/GModel.h"
#include "gmsh/Field.h"
#include "gmsh/GEntity.h"
#include "gmsh/MElement.h"
#include "gmsh/MTetrahedron.h"
#include "gmsh/MLine.h"
#include "gmsh/MTriangle.h"
#include "gmsh/MVertex.h"
#include "gmsh/Context.h"
#include "gmsh/PView.h"
#include "gmsh/PViewDataGModel.h"
#include "gmsh/discreteVertex.h"
#include "gmsh/discreteEdge.h"
#include "gmsh/discreteFace.h"
#include "gmsh/discreteRegion.h"

#include <vector>
#include <map>
#include <iterator>

typedef std::vector<GEntity*> EntityVector;
typedef std::vector<MVertex*> VertexVector;

// Length set to match FIELD_NAME_LEN
char buffer[102]="";

int FluidityNodeOrdering(int type, int node) {
  // This routine maps fluidity ids to (linear) gmsh type vertex ids.
  int quads[]= {0,1,3,2};
  int hexes[]= {0,1,3,2,4,5,7,6};
  switch(type) {
  case TYPE_QUA:
    return quads[node];
  case TYPE_HEX:
    return hexes[node];
  }
  return node;
}


int GmshNodeOrdering(int type, int node) {
  // This routine maps specific gmsh type vertex ids to fluidity ids
  int quads[]= {0,1,3,2};
  int hexes[]= {0,1,3,2,4,5,7,6};
  switch(type) {
  case MSH_QUA_4:
    return quads[node];
  case MSH_HEX_8:
    return hexes[node];
  }
  return node;
}
extern "C" {

  void cgmsh_initialise() {
    // Routine (over) initialises Gmsh functionality
    GmshInitialize();
  }

  void cgmsh_finalise(GModel *&gm) {
    // Routine cleans up Gmsh functionality
    delete gm;
    GmshFinalize();
  }

  void cread_gmsh_file(GModel *&gm, const char* filename) {
    // Fortran accessible interface to fead a Gmsh .msh file
    // known to gmsh into a GModel object
      gm = new GModel;
      // This switch swaps how Gmsh uses the first two integer tags on elements
      CTX::instance()->mesh.switchElementTags=1;
      // Use generic load, rather than readXXX to support multiple formats
      gm->load(filename);
      // This call reads any data beyond the mesh topology
      PView::readMSH(filename);
  }

  void cmesh_gmsh_file(GModel *&gm, char* name) {
    // Load the file "name" into the GModel pointer provided
    if (strlen(name)>0) {
      PView* pv=PView::getViewByName(name);
      if (pv) {
	// This call assigns the PView "name" as the background mesh
	gm->getFields()->setBackgroundMesh(pv->getIndex());
	// use BAMG in 2d
	CTX::instance()->mesh.algo2d = 7;
	CTX::instance()->mesh.algo3d = 7;
      }
    }
    // This causes the meshing to happen
    gm->mesh(gm->getDim());
    CTX::instance()->mesh.switchElementTags=0;
  }

  void cread_gmsh_sizes(GModel *&gm, int &numNodes, int &numFaces,
			int &numElements, bool &haveRegionIDs,
			bool &haveBounds, bool &haveElementOwners,
			bool &haveColumns,
			int &gdim, int &loc, int &sloc) {
    // This utility routine provides Fortran with the sizes to allocate
    // memory for.
    
    // Get the largest dimensionality represented
    gdim = gm->getDim();

    bool physicals = CTX::instance()->mesh.switchElementTags==1;

    numElements = 0;
    numFaces = 0;
    haveRegionIDs = false;
    haveBounds = false;
    haveElementOwners = false;
    haveColumns = not (PView::getViewByName("column_ids") == NULL);
    EntityVector ents;
    EntityVector eles, all_eles;
    EntityVector faces;
    gm->getEntities(ents);
    for (EntityVector::iterator it = ents.begin(); it != ents.end(); ++it) {
      if ((*it)->dim() == gdim) {
	if((*it)->physicals.size()>0) {
	  eles.push_back(*it);
	} else {
	  all_eles.push_back(*it);
	}
      }
      if ((*it)->dim() == gdim-1 && (physicals || (*it)->physicals.size()>0)) 
	  faces.push_back(*it);
    }    // Number of mesh nodes in model

    if (eles.size()==0 && all_eles.size()==1) {
      eles.push_back(all_eles[0]);
    }
    if (eles.size()>0) {
      loc = eles[0]->getMeshElement(0)->getNumVertices();
    } else {
      loc = 0;
    }
    if (faces.size()>0) {
      sloc = faces[0]->getMeshElement(0)->getNumVertices();
    } else {
      sloc = 0;
    }

    for (EntityVector::iterator it = eles.begin(); it != eles.end(); ++it) {
      numElements = numElements + (*it)->getNumMeshElements();
      if (physicals) {
	haveRegionIDs = haveRegionIDs || (*it)->tag()>0;
      } else {
	haveRegionIDs = haveRegionIDs || (*it)->getPhysicalEntities().size()>0;
      }
    }
    
    if(not haveRegionIDs || physicals) {
      eles[0]->addPhysicalEntity(0);
    }
    
    for (EntityVector::iterator it = faces.begin(); it != faces.end(); ++it) {
      if ((*it)->getNumMeshElements()) 
	haveElementOwners = haveElementOwners || 
	  (*it)->getMeshElement(0)->getPartition()>0;
    }
    for (EntityVector::iterator it = faces.begin(); it != faces.end(); ++it) {
      numFaces = numFaces + (*it)->getNumMeshElements();
      haveBounds = haveBounds || (*it)->tag()>0;
      if (physicals) (*it)->addPhysicalEntity(0);
    }

    if (CTX::instance()->mesh.switchElementTags==0) {
      numNodes = gm->indexMeshVertices(false, 0, true);
    } else {
      numNodes = gm->indexMeshVertices(false, 0, false);
    }


  }

  bool cmp_MElement( const MElement* e1, const MElement* e2) {
    return e1->getNum() < e2->getNum();
  }

  void cread_gmsh_element_connectivity(GModel *&gm, int &numElements,
				       int &loc, int *ndglno, int *regionIDs) {

    std::map<MElement*, int> ele_label;
    std::list<MElement*> ele_list;

    int dim = gm->getDim();
    EntityVector ents;
    gm->getEntities(ents);
    for (EntityVector::iterator it = ents.begin(); it != ents.end(); ++it) {
      if ((*it)->dim() == dim) {
        for (unsigned int i=0; i< (*it)->getNumMeshElements(); ++i) {
	  MElement* e = (*it)->getMeshElement(i);
	  ele_list.push_back(e);
	  ele_label[e] = (*it)->tag();
	}
      }
    }

    ele_list.sort(cmp_MElement);
      
    int k=0;
    for (std::list<MElement*>::iterator e = ele_list.begin();
	 e != ele_list.end(); ++e) {
      for (int j=0; j<(*e)->getNumVertices(); ++j) {
	MVertex* v = (*e)->getVertex(FluidityNodeOrdering((*e)->getType(),j));
	ndglno[loc*k+j] = v->getIndex();
      }
      if (regionIDs) {
	int tag = ele_label[*e];
	if (tag>0) { 
	  regionIDs[k] = tag; 
	}
      }
      ++k;
    }
  }

  void cread_gmsh_points(GModel *&gm, int &dim, int &numNodes, double *val) {
    EntityVector ents;
    gm->getEntities(ents);
    for (EntityVector::iterator it = ents.begin(); it != ents.end(); ++it) {
      for (unsigned int i=0; i<(*it)->mesh_vertices.size(); ++i) {
	MVertex *v = (*it)->mesh_vertices[i];
	int idx = v->getIndex();
	if (idx>-1) {
	  switch(dim) {
	  case 3:
	    val[dim*(idx-1)+2] = v->z(); 
	  case 2:
	    val[dim*(idx-1)+1] = v->y(); 
	  case 1:
	    val[dim*(idx-1)+0] = v->x(); 
	  }
	}
      }
    }
  }

  void cread_gmsh_face_connectivity(GModel *&gm, int &numFaces,
				    int &sloc, int *sndglno,
				    bool &haveBounds, int *boundaryIDs,
				    bool &haveElementOwners, int *faceOwner) {

    EntityVector ents;
    EntityVector faces;
    gm->getEntities(ents);

    bool physicals = CTX::instance()->mesh.switchElementTags==1;

    int dim = gm->getDim();

    std::map<MElement*, int> face_label;
    std::list<MElement*> face_list;

    for (EntityVector::iterator it = ents.begin(); it != ents.end(); ++it) {
      if ((*it)->physicals.size()>0 && (*it)->dim() == dim-1) {
	for (unsigned int i=0; i< (*it)->getNumMeshElements(); ++i) {
	  MElement* e = (*it)->getMeshElement(i);
	  face_list.push_back(e);
	  if (physicals) {
	    face_label[e] = (*it)->tag();
	  } else {
	    face_label[e] = (*it)->getPhysicalEntities()[0];
	  }
	}
      }
    }

    face_list.sort(cmp_MElement);
  
    unsigned int k=0;
    for (std::list<MElement*>::iterator e = face_list.begin();
	 e != face_list.end(); ++e) {
      for (int j=0; j<(*e)->getNumVertices(); ++j) {
	MVertex* v = (*e)->getVertex(FluidityNodeOrdering((*e)->getType(),j));
	sndglno[sloc*k+j] = v->getIndex();
      }
      if (haveBounds) {
	int tag = face_label[*e];
	if (tag>0) {
	  boundaryIDs[k] = tag;
	} else {
	  boundaryIDs[k] = 0;
	}
      }
      if (haveElementOwners) {
	if((*e)->getPartition()>0) {
	  faceOwner[k] = (*e)->getPartition();
	}
      }     
      ++k;
    }
  }

  void cmesh_to_gmodel(GModel *&gm,
		       int &numNodes, int &numElements, int &numFaces,
		       int &loc, int &sloc,
		       int &gdim, int &pdim, double* val, 
		       int &etype, int* eles,
		       int &ftype, int* faces,
		       int *ele_ids, int *face_ids, int *eleOwners) {
    
    gm = new GModel;

    GEntity *e;
    std::map<int,GVertex*> point;
    std::map<int,GEdge*> edge;
    std::map<int,GFace*> face;
    std::map<int,GRegion*> region;
    
    std::set<int> uele_ids;
    std::set<int> uface_ids;
    
    uele_ids.insert(0);
    if (ele_ids) {
      for (int i=0;i<numElements;++i) {
	uele_ids.insert(ele_ids[i]);
      }
    } 
    uface_ids.insert(0); 
    if(face_ids) {
      for (int i=0;i<numFaces;++i) {
	uface_ids.insert(face_ids[i]);
      }
    }

    switch(gdim) {
    case 1:
      for (std::set<int>::iterator it=uele_ids.begin();
	   it != uele_ids.end(); ++it) {
	edge[*it] = new discreteEdge(gm,*it,NULL,NULL);
	edge[*it]->addPhysicalEntity(std::max(1,*it));
	edge[*it]->setTag(*it);
	gm->add(edge[*it]);
      }
      e = edge[0];
      for (std::set<int>::iterator it=uface_ids.begin();
	   it != uface_ids.end(); ++it) {
	point[*it] = new discreteVertex(gm,*it);
	point[*it]->addPhysicalEntity(*it);
	point[*it]->setTag(*it);
	gm->add(point[*it]);
      }
      break;
    case 2:
      for (std::set<int>::iterator it=uele_ids.begin();
	   it != uele_ids.end(); ++it) {
	face[*it] =  new discreteFace(gm,*it);
	face[*it]->addPhysicalEntity(std::max(1,*it));
	face[*it]->setTag(*it);
	gm->add(face[*it]);
      }
      e=face[0];
      for (std::set<int>::iterator it=uface_ids.begin();
	   it != uface_ids.end(); ++it) {
	edge[*it] = new discreteEdge(gm,*it,NULL,NULL);
	edge[*it]->addPhysicalEntity(*it);
	edge[*it]->setTag(*it);
	gm->add(edge[*it]);
      }
      break;
    case 3:
      for (std::set<int>::iterator it=uele_ids.begin();
	   it != uele_ids.end(); ++it) {
	region[*it] = new discreteRegion(gm,*it);
	region[*it]->addPhysicalEntity(std::max(1,*it));
	region[*it]->setTag(*it);
	gm->add(region[*it]);
      }
      e=region[0];
      for (std::set<int>::iterator it=uface_ids.begin();
	   it != uface_ids.end(); ++it) {
	face[*it] = new discreteFace(gm,*it);
	face[*it]->addPhysicalEntity(*it);
	face[*it]->setTag(*it);
	gm->add(face[*it]);
      }
      break;
    }

    MElementFactory f;

    double x[3] = {0,0,0};
    for (int n=0;n<numNodes;++n) {
      for (int i=0;i<pdim;++i) {
	x[i] = val[pdim*n+i];
      }
      MVertex* v =  new MVertex(x[0],x[1],x[2],NULL,n+1);
      v->setIndex(n+1);
      e->addMeshVertex(v);
    }

    for (int n=0;n<numElements;++n) {
      int id = 0;
      if (ele_ids) id = ele_ids[n];

      VertexVector vertices;
      for (int i=0;i<loc;++i) {
	vertices.push_back(e->getMeshVertex(eles[loc*n+
						 GmshNodeOrdering(etype,i)]-1));
      }
      MElement *ele = f.create(etype, vertices);
      switch(etype) {
      case MSH_LIN_2:
	edge[id]->addLine((MLine*) ele);
	break;
      case MSH_TRI_3:
	face[id]->addTriangle((MTriangle*) ele);
	break;
      case MSH_QUA_4:
	face[id]->addQuadrangle((MQuadrangle*) ele);
	break;
      case MSH_TET_4:
	region[id]->addTetrahedron((MTetrahedron*) ele);
	break;
      case MSH_HEX_8:
	region[id]->addHexahedron((MHexahedron*) ele);
	break;
      }
    }

    for (int n=0;n<numFaces;++n) {
      int id = 0;
      if (face_ids) id = face_ids[n];

      VertexVector vertices;
      for (int i=0;i<sloc;++i) {
	vertices.push_back(e->getMeshVertex(faces[sloc*n
						  +GmshNodeOrdering(ftype,i)]-1));
      }
      MElement *ele = f.create(ftype, vertices);
      if (eleOwners) ele->setPartition(eleOwners[n]);
      switch(ftype) {
      case MSH_PNT:
	point[id]->addPoint((MPoint*) ele);
	break;
      case MSH_LIN_2:
	edge[id]->addLine((MLine*) ele);
	break;
      case MSH_TRI_3:
	face[id]->addTriangle((MTriangle*) ele);
	break;
      case MSH_QUA_4:
	face[id]->addQuadrangle((MQuadrangle*) ele);
	break;
      }
    }    
  }

  void cread_gmsh_node_data(GModel *&gm, char* name, double* data, int &step) {
    PView *pv = PView::getViewByName(name);
    PViewDataGModel *d = dynamic_cast<PViewDataGModel*>(pv->getData());
    int nComp = d->getNumComponents(step,0,0);
    int nNodes = gm->getNumMeshVertices();
    for (int i=0; i<nNodes; ++i) {
      for(int j=0; j<nComp; ++j) {
	d->getValueByIndex(step, i+1, 0 ,j, data[nComp*i+j]);
      }
    }	   
  }

  void cdata_to_pview_node_data(GModel *&gm, PViewDataGModel *& pvd,
				int &numNodes,
				double *data,
				char* name,
				int &numComponents) {
    
    pvd = new PViewDataGModel(PViewDataGModel::NodeData);
    std::map<int, std::vector<double> > vdata;
    int k = 0;
    for (int n=0;n<numNodes;++n) {
      std::vector<double> lvec;
      for (int i=0;i<numComponents;++i) {
	lvec.push_back(data[k++]);
      }
      vdata[n+1] = lvec; 
    }
    pvd->addData( gm, vdata, 0, 0.0, 1, numComponents);
    pvd->setName(name);
    new PView(pvd);
  }

  void cwrite_gmsh_file(GModel *&gm, bool &binary, char* filename) {
    gm->writeMSH(filename, 2.2, binary);
  }
  
  void cwrite_gmsh_data_file(PViewDataGModel *& pvd,  bool &binary,
			     char* filename) {
    pvd->writeMSH(filename, 2.2, binary, false, true);
  }

  int cgmsh_count_physical_names(GModel *& gm, int &dim) {

    int count = 0;
    for (GModel::piter it=gm->firstPhysicalName();
	   it != gm->lastPhysicalName(); ++it) {
      if (it->first.first == dim) ++count;
    }
    return count;
  }

  bool cget_gmsh_physical_name(GModel *& gm, GModel::piter *&it, int &mdim,
			       int &idx, char* &c_string) {
    if (it== NULL) {
      it = new GModel::piter;
      (*it) = gm->firstPhysicalName();
    }
    while ((*it) != gm->lastPhysicalName() && (*it)->first.first !=mdim) {
      ++(*it);
    }
    if ((*it) != gm->lastPhysicalName()) {
      idx = (*it)->first.second;
      buffer[(*it)->second.copy(buffer, 40)] = '\0';
      c_string = buffer;
      ++(*it);
      return true;
    } else {
      delete it;
      return false;
    }
  }

}

#endif
