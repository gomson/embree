// ======================================================================== //
// Copyright 2009-2016 Intel Corporation                                    //
//                                                                          //
// Licensed under the Apache License, Version 2.0 (the "License");          //
// you may not use this file except in compliance with the License.         //
// You may obtain a copy of the License at                                  //
//                                                                          //
//     http://www.apache.org/licenses/LICENSE-2.0                           //
//                                                                          //
// Unless required by applicable law or agreed to in writing, software      //
// distributed under the License is distributed on an "AS IS" BASIS,        //
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. //
// See the License for the specific language governing permissions and      //
// limitations under the License.                                           //
// ======================================================================== //

#pragma once

#include "geometry.h"
#include "buffer.h"
#include "../subdiv/half_edge.h"
#include "../subdiv/tessellation_cache.h"
#include "../subdiv/catmullclark_coefficients.h"
#include "../subdiv/patch.h"
#include "../../common/algorithms/parallel_map.h"
#include "../../common/algorithms/parallel_set.h"

namespace embree
{
  class SubdivMesh : public Geometry
  {
    ALIGNED_CLASS;
  public:

    /*! type of this geometry */
    static const Geometry::Type geom_type = Geometry::SUBDIV_MESH;

    struct Edge 
    {
      /*! edge constructor */
      __forceinline Edge(const uint32_t v0, const uint32_t v1)
	: v0(v0), v1(v1) {}

      /*! create an 64 bit identifier that is unique for the not oriented edge */
      __forceinline operator uint64_t() const       
      {
	uint32_t p0 = v0, p1 = v1;
	if (p0<p1) std::swap(p0,p1);
	return (((uint64_t)p0) << 32) | (uint64_t)p1;
      }

    public:
      uint32_t v0,v1;    //!< start and end vertex of the edge
    };

    /*! structure used to sort half edges using radix sort by their key */
    struct KeyHalfEdge 
    {
      KeyHalfEdge() {}
      
      KeyHalfEdge (uint64_t key, HalfEdge* edge) 
      : key(key), edge(edge) {}
      
      __forceinline operator uint64_t() const { 
	return key; 
      }

      friend __forceinline bool operator<(const KeyHalfEdge& e0, const KeyHalfEdge& e1) {
        return e0.key < e1.key;
      }
      
    public:
      uint64_t key;
      HalfEdge* edge;
    };

  public:

    /*! subdiv mesh construction */
    SubdivMesh(Scene* parent, RTCGeometryFlags flags, size_t numFaces, size_t numEdges, size_t numVertices, 
               size_t numCreases, size_t numCorners, size_t numHoles, size_t numTimeSteps);

  public:
    void enabling();
    void disabling();
    void setMask (unsigned mask);
    void setBoundaryMode (RTCBoundaryMode mode);
    void setBuffer(RTCBufferType type, void* ptr, size_t offset, size_t stride);
    void* map(RTCBufferType type);
    void unmap(RTCBufferType type);
    void update ();
    void updateBuffer (RTCBufferType type);
    void setTessellationRate(float N);
    void immutable ();
    bool verify ();
    void setDisplacementFunction (RTCDisplacementFunc func, RTCBounds* bounds);
    void setDisplacementFunction2 (RTCDisplacementFunc2 func, RTCBounds* bounds);
    void interpolate(unsigned primID, float u, float v, RTCBufferType buffer, float* P, float* dPdu, float* dPdv, float* ddPdudu, float* ddPdvdv, float* ddPdudv, size_t numFloats);
    void interpolateN(const void* valid_i, const unsigned* primIDs, const float* u, const float* v, size_t numUVs, 
                      RTCBufferType buffer, float* P, float* dPdu, float* dPdv, float* ddPdudu, float* ddPdvdv, float* ddPdudv, size_t numFloats);

  public:

    /*! return the number of faces */
    size_t size() const { 
      return numFaces; 
    };

    /*! calculates the bounds of the i'th subdivision patch at the j'th timestep */
    __forceinline BBox3fa bounds(size_t i, size_t j = 0) const {
      return halfEdges[faceStartEdge[i]].bounds(vertices[j]);
    }

    /*! check if the i'th primitive is valid */
    __forceinline bool valid(size_t i) const 
    {
      if (unlikely(boundary == RTC_BOUNDARY_NONE)) {
        if (getHalfEdge(i)->faceHasBorder()) return false;
      }
      return !invalidFace(i);
    }

    /*! check if the i'th primitive is valid for the j'th time range */
    __forceinline bool valid(size_t i, size_t j) const 
    {
      if (unlikely(boundary == RTC_BOUNDARY_NONE)) {
        if (getHalfEdge(i)->faceHasBorder()) return false;
      }
      return !invalidFace(i,j);
    }

    /*! initializes the half edge data structure */
    void initializeHalfEdgeStructures ();
 
  private:

    /*! recalculates the half edges */
    void calculateHalfEdges();

    /*! updates half edges when recalculation is not necessary */
    void updateHalfEdges();

  public:

    /*! returns the start half edge for some face */
    __forceinline const HalfEdge* getHalfEdge ( const size_t f ) const { 
      return &halfEdges[faceStartEdge[f]]; 
    }    

    /*! returns the vertex buffer for some time step */
    __forceinline const BufferRefT<Vec3fa>& getVertexBuffer( const size_t t = 0 ) const {
      return vertices[t];
    }

     /* check for simple edge level update */
    __forceinline bool checkLevelUpdate() const { return levelUpdate; }

    /* returns tessellation level of edge */
    __forceinline float getEdgeLevel(const size_t i) const
    {
      if (levels) return clamp(levels[i],1.0f,4096.0f); // FIXME: do we want to limit edge level?
      else return clamp(tessellationRate,1.0f,4096.0f); // FIXME: do we want to limit edge level?
    }

  private:
    size_t numFaces;           //!< number of faces
    size_t numEdges;           //!< number of edges
    size_t numVertices;        //!< number of vertices
    RTCBoundaryMode boundary;  //!< boundary interpolation mode

  public:
    RTCDisplacementFunc displFunc;    //!< displacement function
    RTCDisplacementFunc2 displFunc2;    //!< displacement function
    BBox3fa             displBounds;  //!< bounds for maximal displacement 

    /*! all buffers in this section are provided by the application */
  public:
    
    /*! buffer containing the number of vertices for each face */
    APIBuffer<unsigned> faceVertices;

    /*! indices of the vertices composing each face */
    APIBuffer<unsigned> vertexIndices;

    /*! vertex buffer (one buffer for each time step) */
    vector<APIBuffer<Vec3fa>> vertices;

    /*! user data buffers */
    array_t<std::unique_ptr<APIBuffer<char>>,2> userbuffers;

    /*! edge crease buffer containing edges (pairs of vertices) that carry edge crease weights */
    APIBuffer<Edge> edge_creases;

    /*! edge crease weights for each edge of the edge_creases buffer */
    APIBuffer<float> edge_crease_weights;

    /*! vertex crease buffer containing all vertices that carry vertex crease weights */
    APIBuffer<unsigned> vertex_creases;

    /*! vertex crease weights for each vertex of the vertex_creases buffer */
    APIBuffer<float> vertex_crease_weights;

    /*! subdivision level for each half edge of the vertexIndices buffer */
    APIBuffer<float> levels;
    float tessellationRate;  // constant rate that is used when levels is not set

    /*! buffer that marks specific faces as holes */
    APIBuffer<unsigned> holes;

    /*! all data in this section is generated by initializeHalfEdgeStructures function */
  private:

    /*! number of half edges used by faces */
    size_t numHalfEdges; 

    /*! fast lookup table to find the first half edge for some face */
    mvector<uint32_t> faceStartEdge;

    /*! Half edge structure. */
    mvector<HalfEdge> halfEdges;

    /*! set with all holes */
    parallel_set<uint32_t> holeSet;

    /*! fast lookup table to detect invalid faces */
    mvector<char> invalid_face;

    /*! test if face i is invalid in timestep j */
    __forceinline       char& invalidFace(size_t i, size_t j = 0)       { return invalid_face[i*numTimeSteps+j]; }
    __forceinline const char& invalidFace(size_t i, size_t j = 0) const { return invalid_face[i*numTimeSteps+j]; }

    /*! flag whether only the edge levels have changed and the mesh has no creases,
     *  allows for simple bvh update instead of full rebuild in cached mode */
    bool levelUpdate;

    /*! interpolation cache */
  public:
    static __forceinline size_t numInterpolationSlots4(size_t stride) { return (stride+15)/16; }
    static __forceinline size_t numInterpolationSlots8(size_t stride) { return (stride+31)/32; }
    static __forceinline size_t interpolationSlot(size_t prim, size_t slot, size_t stride) {
#if defined (__AVX__)
      const size_t slots = numInterpolationSlots8(stride); 
#else
      const size_t slots = numInterpolationSlots4(stride); 
#endif
      assert(slot < slots); 
      return slots*prim+slot;
    }
    std::vector<std::vector<SharedLazyTessellationCache::CacheEntry>> vertex_buffer_tags;
    std::vector<SharedLazyTessellationCache::CacheEntry> user_buffer_tags[2];
    std::vector<Patch3fa::Ref> patch_eval_trees;
      
    /*! the following data is only required during construction of the
     *  half edge structure and can be cleared for static scenes */
  private:

    /*! two arrays used to sort the half edges */
    std::vector<KeyHalfEdge> halfEdges0;
    std::vector<KeyHalfEdge> halfEdges1;

    /*! map with all vertex creases */
    parallel_map<uint32_t,float> vertexCreaseMap;

    /*! map with all edge creases */
    parallel_map<uint64_t,float> edgeCreaseMap;
  };

  class SubdivMeshAVX : public SubdivMesh
  {
  public:
    //using SubdivMesh::SubdivMesh; // inherit all constructors // FIXME: compiler bug under VS2013
    SubdivMeshAVX (Scene* parent, RTCGeometryFlags flags, size_t numFaces, size_t numEdges, size_t numVertices, 
                  size_t numCreases, size_t numCorners, size_t numHoles, size_t numTimeSteps);

    void interpolate(unsigned primID, float u, float v, RTCBufferType buffer, float* P, float* dPdu, float* dPdv, float* ddPdudu, float* ddPdvdv, float* ddPdudv, size_t numFloats);
    void interpolateN(const void* valid_i, const unsigned* primIDs, const float* u, const float* v, size_t numUVs, 
                      RTCBufferType buffer, float* P, float* dPdu, float* dPdv, float* ddPdudu, float* ddPdvdv, float* ddPdudv, size_t numFloats);

    template<typename vbool, typename vint, typename vfloat>
      void interpolateHelper(const vbool& valid1, const vint& primID, const vfloat& uu, const vfloat& vv, size_t numUVs, 
                             RTCBufferType buffer, float* P, float* dPdu, float* dPdv, float* ddPdudu, float* ddPdvdv, float* ddPdudv, size_t numFloats);
  };
};
