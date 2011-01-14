/*
 *    Copyright (C) 2010 Imperial College London and others.
 *    
 *    Please see the AUTHORS file in the main source directory for a full list
 *    of copyright holders.
 *
 *    Gerard Gorman
 *    Applied Modelling and Computation Group
 *    Department of Earth Science and Engineering
 *    Imperial College London
 *
 *    amcgsoftware@imperial.ac.uk
 *    
 *    This library is free software; you can redistribute it and/or
 *    modify it under the terms of the GNU Lesser General Public
 *    License as published by the Free Software Foundation,
 *    version 2.1 of the License.
 *
 *    This library is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *    Lesser General Public License for more details.
 *
 *    You should have received a copy of the GNU Lesser General Public
 *    License along with this library; if not, write to the Free Software
 *    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307
 *    USA
 */

#ifndef METRICFIELD_H
#define METRICFIELD_H

#include <cmath>
#include <iostream>
#include <map>
#include <set>
#include <vector>

#include <Eigen/Core>
#include <Eigen/Dense>

#include "MetricTensor.h"
#include "Metis.h"
#include "Surface.h"
#include "LipnikovFunctional.h"

#ifdef HAVE_MPI
#include <mpi.h>
#endif

#include <omp.h>

/*! \brief Constructs metric tensor field which encodes anisotropic
 *  edge size information.
 * 
 * Use this class to create a metric tensor field using desired error
 * tolerences and curvature of linear fields subject to a set of constraints.
 * index_t should be set as int32_t for problems with less than 2 billion nodes.
 * For larger meshes int64_t should be used.
 */
template<typename real_t, typename index_t>
  class MetricField{
 public:
  
  /*! Default constructor.
   */
  MetricField(){
    _NNodes = 0;
    _NElements = 0;
    _ndims = 0;
    nloc = 0;
    _ENList = NULL;
    _node_distribution = NULL;
    _x = NULL;
    _y = NULL;
    _z = NULL;
#ifdef HAVE_MPI
    _mesh_comm = NULL;
#endif
    _metric = NULL;
    _surface = NULL;
  }

  /*! Default destructor.
   */
  ~MetricField(){
    if(_metric!=NULL)
      delete [] _metric;
  }
  
  /*! Set the source mesh (2D triangular meshes).
   * @param NNodes number of nodes in the local mesh.
   * @param NElements number of nodes in the local mesh.
   * @param ENList array storing the global node number for each element.
   * @param x is the X coordinate.
   * @param y is the Y coordinate.
   * @param node_distribution gives the number of nodes owned
   *        by each process when this is using MPI, where it is
   *        assumed that each processes ownes n_i consecutive
   *        vertices of the mesh. Its contents are identical for every process.
   */
  void set_mesh(int NNodes, int NElements, const index_t *ENList, Surface<real_t, index_t> *surface,
                const real_t *x, const real_t *y, const index_t *node_distribution=NULL){
    nloc = 3;
    _ndims = 2;
    _NNodes = NNodes;
    _NElements = NElements;
    _ENList = ENList;
    _surface = surface;
    _x = x;
    _y = y;
    _z = NULL;
    _node_distribution = node_distribution;

    real_t bbox[] = {x[0], x[0], y[0], y[0]};
    for(int i=1;i<_NNodes;i++){
      bbox[0] = min(bbox[0], x[i]); bbox[1] = max(bbox[1], x[i]);
      bbox[2] = min(bbox[2], y[i]); bbox[3] = max(bbox[3], y[i]);
    }
    _metric = new MetricTensor<real_t>[_NNodes];
    real_t m[] = {1.0/pow(bbox[1]-bbox[0], 2), 0, 0, 1.0/pow(bbox[3]-bbox[2], 2)};
    for(int i=0;i<_NNodes;i++){
      _metric[i].set(_ndims, m);
    }
  }
  
  /*! Set the source mesh (3D tetrahedral meshes).
   * @param NNodes number of nodes in the local mesh.
   * @param NElements number of nodes in the local mesh.
   * @param ENList array storing the global node number for each element.
   * @param x is the X coordinate.
   * @param y is the Y coordinate.
   * @param z is the Z coordinate.
   * @param node_distribution gives the number of nodes owned
   *        by each process when this is using MPI, where it is
   *        assumed that each processes ownes n_i consecutive
   *        vertices of the mesh. Its contents are identical for every process.
   */
  void set_mesh(int NNodes, int NElements, const index_t *ENList, Surface<real_t, index_t> *surface,
                const real_t *x, const real_t *y, const real_t *z, const index_t *node_distribution=NULL){
    nloc = 4;
    _ndims = 3;
    _NNodes = NNodes;
    _NElements = NElements;
    _ENList = ENList;
    _surface = surface;
    _x = x;
    _y = y;
    _z = z;
    _node_distribution = node_distribution;

    real_t bbox[] = {x[0], x[0], y[0], y[0], z[0], z[0]};
    for(int i=1;i<_NNodes;i++){
      bbox[0] = min(bbox[0], x[i]); bbox[1] = max(bbox[1], x[i]);
      bbox[2] = min(bbox[2], y[i]); bbox[3] = max(bbox[3], y[i]);
      bbox[4] = min(bbox[4], z[i]); bbox[5] = max(bbox[5], z[i]);
    }
    _metric = new MetricTensor<real_t>[_NNodes];
    real_t m[] = {1.0/pow(bbox[1]-bbox[0], 2), 0, 0,
                  0, 1.0/pow(bbox[3]-bbox[2], 2), 0,
                  0, 0, 1.0/pow(bbox[5]-bbox[4], 2)};

    for(int i=0;i<_NNodes;i++){
      _metric[i].set(_ndims, m);
    }
  }

  /*! Copy back the metric tensor field.
   * @param metric is a pointer to the buffer where the metric field can be copied.
   */
  void get_metric(real_t *metric){
    for(int i=0;i<_NNodes;i++){
      _metric[i].get_metric(metric+i*_ndims*_ndims);
    }
  }

#ifdef HAVE_MPI
  /*! Set the MPI communicator to be used.
   * @param mesh_comm is the MPI communicator.
   */
  void set_mpi_communicator(const MPI_Comm *mesh_comm){
    _mesh_comm = mesh_comm;
  }
#endif

  /*! Add the contribution from the metric field from a new field with a target linear interpolation error.
   * @param psi is field while curvature is to be considered.
   * @param target_error is the target interpolation error.
   * @param sigma_psi should be set if a relative interpolation error is specified. It is the minimum value for psi used when applying the relative error. If the argument is not specified or less than 0 then an absolute error is assumed.
   */
  void add_field(const real_t *psi, const real_t target_error, real_t sigma_psi=-1.0){
    bool relative = sigma_psi>0.0;

    int ndims2 = _ndims*_ndims;
    
    real_t *Hessian = new real_t[_NNodes*ndims2];
    
    get_hessian(psi, Hessian);
    
    if(relative){
      for(int i=0;i<_NNodes;i++){
        real_t eta = 1.0/max(target_error*psi[i], sigma_psi);
        for(int j=0;j<ndims2;j++)
          Hessian[i*ndims2+j]*=eta;
      }
    }else{
      for(int i=0;i<_NNodes;i++){
        real_t eta = 1.0/target_error;
        for(int j=0;j<ndims2;j++)
          Hessian[i*ndims2+j]*=eta;
      }
    }
    
#pragma omp parallel
    {
#pragma omp for schedule(static)
      for(int ip=0;ip<_NNodes;ip++){
        size_t i=norder[ip];
        _metric[i].constrain(MetricTensor<real_t>(_ndims, Hessian+i*ndims2));
      }
    }

    delete [] Hessian;
  }

  /*! Apply maximum edge length constraint.
   * @param max_len specifies the maximum allowed edge length.
   */
  void apply_max_edge_length(real_t max_len){
    real_t M[_ndims*_ndims];
    for(int i=0;i<_ndims;i++)
      for(int j=0;j<_ndims;j++)
        if(i==j)
          M[i*_ndims+j] = 1.0/(max_len*max_len);
        else
          M[i*_ndims+j] = 0.0;
    
    MetricTensor<real_t> constraint(_ndims, M);
    
    for(size_t i=0;i<_NNodes;i++)
      _metric[i].constrain(constraint);
  }
  
  /*! Apply minimum edge length constraint.
   * @param min_len specifies the minimum allowed edge length.
   */
  void apply_min_edge_length(real_t min_len){
    real_t M[_ndims*_ndims];
    for(int i=0;i<_ndims;i++)
      for(int j=0;j<_ndims;j++)
        if(i==j)
          M[i*_ndims+j] = 1.0/(min_len*min_len);
        else
          M[i*_ndims+j] = 0.0;
    
    MetricTensor<real_t> constraint(_ndims, M);
    
    for(int i=0;i<_NNodes;i++)
      _metric[i].constrain(constraint, false);
  }
  
  /*! Apply maximum aspect ratio constraint.
   * @param max_aspect_ratio maximum aspect ratio for elements.
   */
  void apply_max_aspect_ratio(real_t max_aspect_ratio){
    std::cerr<<"ERROR: Not yet implemented\n";
  }

  /*! Apply maximum number of elements constraint.
   * @param max_nelements the maximum number of elements desired.
   */
  void apply_max_nelements(real_t max_nelements){
    int predicted = predict_nelements();
    if(predicted>max_nelements){
      real_t scale_factor = sqrt(max_nelements/predicted);
      
      for(int i=0;i<_NNodes;i++)
        _metric[i].scale(scale_factor);
    }
  }

  /*! Apply minimum number of elements constraint.
   * @param min_nelements the minimum number of elements desired.
   */
  void apply_min_nelements(real_t min_nelements){
    int predicted = predict_nelements();
    if(predicted<min_nelements){
      real_t scale_factor = sqrt(min_nelements/predicted);
      
      for(int i=0;i<_NNodes;i++)
        _metric[i].scale(scale_factor);
    }
  }

  /*! Apply required number of elements.
   * @param nelements is the required number of elements after adapting.
   */
  void apply_nelements(real_t nelements){
    real_t scale_factor = sqrt(nelements/predict_nelements());
    
    for(int i=0;i<_NNodes;i++)
      _metric[i].scale(scale_factor);
  }

  /*! Predict the number of elements when mesh satisifies metric tensor field.
   */
  real_t predict_nelements(){
    real_t predicted=0;

    if(_NElements<1)
      return predicted;

    if(_ndims==2){
      real_t refx0[] = {_x[_ENList[0]], _y[_ENList[0]]};
      real_t refx1[] = {_x[_ENList[1]], _y[_ENList[1]]};
      real_t refx2[] = {_x[_ENList[2]], _y[_ENList[2]]};
      LipnikovFunctional<real_t> functional(refx0, refx1, refx2);

      real_t total_area_metric = 0.0;
      for(int i=0;i<_NElements;i++){
        const index_t *n=_ENList+nloc*i;
        real_t x0[] = {_x[n[0]], _y[n[0]]};
        real_t x1[] = {_x[n[1]], _y[n[1]]};
        real_t x2[] = {_x[n[2]], _y[n[2]]};
        real_t area = functional.area(x0, x1, x2);
        
        const real_t *m0=_metric[n[0]].get_metric();
        const real_t *m1=_metric[n[1]].get_metric();
        const real_t *m2=_metric[n[2]].get_metric();

        real_t m00 = (m0[0]+m1[0]+m2[0])/3;
        real_t m01 = (m0[1]+m1[1]+m2[1])/3;
        real_t m11 = (m0[3]+m1[3]+m2[3])/3;

        real_t det = m00*m11-m01*m01;
        total_area_metric += area*det;
      }

      // Ideal area of triangle in metric space.
      double ideal_area = sqrt(3.0)/4.0;
      
      predicted = total_area_metric/ideal_area;
    }else{
      real_t refx0[] = {_x[_ENList[0]], _y[_ENList[0]], _z[_ENList[0]]};
      real_t refx1[] = {_x[_ENList[1]], _y[_ENList[1]], _z[_ENList[1]]};
      real_t refx2[] = {_x[_ENList[2]], _y[_ENList[2]], _z[_ENList[2]]};
      real_t refx3[] = {_x[_ENList[3]], _y[_ENList[3]], _z[_ENList[3]]};
      LipnikovFunctional<real_t> functional(refx0, refx1, refx2, refx3);
      
      real_t total_volume_metric = 0.0;
      for(int i=0;i<_NElements;i++){
        const index_t *n=_ENList+nloc*i;
        real_t x0[] = {_x[_ENList[0]], _y[_ENList[0]], _z[_ENList[0]]};
        real_t x1[] = {_x[_ENList[1]], _y[_ENList[1]], _z[_ENList[1]]};
        real_t x2[] = {_x[_ENList[2]], _y[_ENList[2]], _z[_ENList[2]]};
        real_t x3[] = {_x[_ENList[3]], _y[_ENList[3]], _z[_ENList[3]]};
        real_t volume = functional.volume(x0, x1, x2, x3);
        
        const real_t *m0=_metric[n[0]].get_metric();
        const real_t *m1=_metric[n[1]].get_metric();
        const real_t *m2=_metric[n[2]].get_metric();
        const real_t *m3=_metric[n[3]].get_metric();

        real_t m00 = (m0[0]+m1[0]+m2[0]+m3[0])/4;
        real_t m01 = (m0[1]+m1[1]+m2[1]+m3[1])/4;
        real_t m02 = (m0[2]+m1[2]+m2[2]+m3[2])/4;
        real_t m11 = (m0[4]+m1[4]+m2[4]+m3[4])/4;
        real_t m12 = (m0[5]+m1[5]+m2[5]+m3[5])/4;
        real_t m22 = (m0[8]+m1[8]+m2[8]+m3[8])/4;


        real_t det = (m11*m22 - m12*m12)*m00 - (m01*m22 - m02*m12)*m01 + (m01*m12 - m02*m11)*m02;
        total_volume_metric += volume*det;
      }

      // Ideal volume of triangle in metric space.
      double ideal_volume = 1.0/sqrt(72.0);
      
      predicted = total_volume_metric/ideal_volume;
    }

    return predicted;
  }

 private:
  /*! Apply required number of elements.
   * @param psi is the node wise field variable.
   * @param Hessian is the location where the Hessian of psi is copied.
   */
  void get_hessian(const real_t *psi, real_t *Hessian){
    
    // Create node-node list
    if(NNList.empty()){
      NNList.resize(_NNodes);
      for(int i=0; i<_NElements; i++){
        for(int j=0;j<nloc;j++){
          for(int k=j+1;k<nloc;k++){
            NNList[_ENList[i*nloc+j]].insert(_ENList[i*nloc+k]);
            NNList[_ENList[i*nloc+k]].insert(_ENList[i*nloc+j]);
          }
        }
      }
      Metis<index_t>::reorder(NNList, norder);
    }

#pragma omp parallel
    {
      // Calculate Hessian at each point.
      size_t min_patch_size = (_ndims==2)?7:11; // number of parameters + 1
#pragma omp for schedule(static)
      for(int ip=0; ip<_NNodes; ip++){
        size_t i=norder[ip];
        std::set<index_t> patch = NNList[i];
        
        if((patch.size()<min_patch_size)||(_surface->contains_node(i))){
          std::set<index_t> front = NNList[i];
          for(typename std::set<index_t>::const_iterator it=front.begin();it!=front.end();it++){
            patch.insert(NNList[*it].begin(), NNList[*it].end());
          }
          if(patch.size()<min_patch_size){
            cerr<<"WARNING: Small mesh patch detected.\n";  
          }
        }
        
        if(_ndims==2){
          // Form quadratic system to be solved. The quadratic fit is:
          // P = a0+a1x+a2y+a3xy+a4x^2+a5y^2
          // A = P^TP
          double x=_x[i], y=_y[i];
          Eigen::Matrix<real_t, Eigen::Dynamic, Eigen::Dynamic> A = Eigen::Matrix<real_t, Eigen::Dynamic, Eigen::Dynamic>::Zero(6,6);
          
          A[0]+=pow(y,4); A[1]+=pow(x,2)*pow(y,2); A[2]+=x*pow(y,3); A[3]+=pow(y,3); A[4]+=x*pow(y,2); A[5]+=pow(y,2);
          A[6]+=pow(x,2)*pow(y,2); A[7]+=pow(x,4); A[8]+=pow(x,3)*y; A[9]+=pow(x,2)*y; A[10]+=pow(x,3); A[11]+=pow(x,2);
          A[12]+=x*pow(y,3); A[13]+=pow(x,3)*y; A[14]+=pow(x,2)*pow(y,2); A[15]+=x*pow(y,2); A[16]+=pow(x,2)*y; A[17]+=x*y;
          A[18]+=pow(y,3); A[19]+=pow(x,2)*y; A[20]+=x*pow(y,2); A[21]+=pow(y,2); A[22]+=x*y; A[23]+=y;
          A[24]+=x*pow(y,2); A[25]+=pow(x,3); A[26]+=pow(x,2)*y; A[27]+=x*y; A[28]+=pow(x,2); A[29]+=x;
          A[30]+=pow(y,2); A[31]+=pow(x,2); A[32]+=x*y; A[33]+=y; A[34]+=x; A[35]+=1;
          
          Eigen::Matrix<real_t, Eigen::Dynamic, 1> b = Eigen::Matrix<real_t, Eigen::Dynamic, 1>::Zero(6);
          
          b[0]+=psi[i]*pow(y,2); b[1]+=psi[i]*pow(x,2); b[2]+=psi[i]*x*y; b[3]+=psi[i]*y; b[4]+=psi[i]*x; b[5]+=psi[i]*1;
          
          for(typename std::set<index_t>::const_iterator n=patch.begin(); n!=patch.end(); n++){
            x=_x[*n]; y=_y[*n];
            
            A[0]+=pow(y,4); A[1]+=pow(x,2)*pow(y,2); A[2]+=x*pow(y,3); A[3]+=pow(y,3); A[4]+=x*pow(y,2); A[5]+=pow(y,2);
            A[6]+=pow(x,2)*pow(y,2); A[7]+=pow(x,4); A[8]+=pow(x,3)*y; A[9]+=pow(x,2)*y; A[10]+=pow(x,3); A[11]+=pow(x,2);
            A[12]+=x*pow(y,3); A[13]+=pow(x,3)*y; A[14]+=pow(x,2)*pow(y,2); A[15]+=x*pow(y,2); A[16]+=pow(x,2)*y; A[17]+=x*y;
            A[18]+=pow(y,3); A[19]+=pow(x,2)*y; A[20]+=x*pow(y,2); A[21]+=pow(y,2); A[22]+=x*y; A[23]+=y;
            A[24]+=x*pow(y,2); A[25]+=pow(x,3); A[26]+=pow(x,2)*y; A[27]+=x*y; A[28]+=pow(x,2); A[29]+=x;
            A[30]+=pow(y,2); A[31]+=pow(x,2); A[32]+=x*y; A[33]+=y; A[34]+=x; A[35]+=1;
            
            b[0]+=psi[*n]*pow(y,2); b[1]+=psi[*n]*pow(x,2); b[2]+=psi[*n]*x*y; b[3]+=psi[*n]*y; b[4]+=psi[*n]*x; b[5]+=psi[*n]*1;
          }
          
          Eigen::Matrix<real_t, Eigen::Dynamic, 1> a = Eigen::Matrix<real_t, Eigen::Dynamic, 1>::Zero(6);
          A.lu().solve(b, &a);
          
          Hessian[i*4  ] = 2*a[1]; // d2/dx2
          Hessian[i*4+1] = a[2];   // d2/dxdy
          Hessian[i*4+2] = a[2];   // d2/dxdy
          Hessian[i*4+3] = 2*a[0]; // d2/dy2
        }else{
          // Form quadratic system to be solved. The quadratic fit is:
          // P = 1 + x + y + z + x^2 + y^2 + z^2 + xy + xz + yz
          // A = P^TP
          double x=_x[i], y=_y[i], z=_z[i];
          Eigen::Matrix<real_t, Eigen::Dynamic, Eigen::Dynamic> A = Eigen::Matrix<real_t, Eigen::Dynamic, Eigen::Dynamic>::Zero(10,10);
          
          A[0]+=1; A[1]+=x; A[2]+=y; A[3]+=z; A[4]+=pow(x,2); A[5]+=x*y; A[6]+=x*z; A[7]+=pow(y,2); A[8]+=y*z; A[9]+=pow(z,2);
          A[10]+=x; A[11]+=pow(x,2); A[12]+=x*y; A[13]+=x*z; A[14]+=pow(x,3); A[15]+=pow(x,2)*y; A[16]+=pow(x,2)*z; A[17]+=x*pow(y,2); A[18]+=x*y*z; A[19]+=x*pow(z,2);
          A[20]+=y; A[21]+=x*y; A[22]+=pow(y,2); A[23]+=y*z; A[24]+=pow(x,2)*y; A[25]+=x*pow(y,2); A[26]+=x*y*z; A[27]+=pow(y,3); A[28]+=pow(y,2)*z; A[29]+=y*pow(z,2);
          A[30]+=z; A[31]+=x*z; A[32]+=y*z; A[33]+=pow(z,2); A[34]+=pow(x,2)*z; A[35]+=x*y*z; A[36]+=x*pow(z,2); A[37]+=pow(y,2)*z; A[38]+=y*pow(z,2); A[39]+=pow(z,3);
          A[40]+=pow(x,2); A[41]+=pow(x,3); A[42]+=pow(x,2)*y; A[43]+=pow(x,2)*z; A[44]+=pow(x,4); A[45]+=pow(x,3)*y; A[46]+=pow(x,3)*z; A[47]+=pow(x,2)*pow(y,2); A[48]+=pow(x,2)*y*z; A[49]+=pow(x,2)*pow(z,2);
          A[50]+=x*y; A[51]+=pow(x,2)*y; A[52]+=x*pow(y,2); A[53]+=x*y*z; A[54]+=pow(x,3)*y; A[55]+=pow(x,2)*pow(y,2); A[56]+=pow(x,2)*y*z; A[57]+=x*pow(y,3); A[58]+=x*pow(y,2)*z; A[59]+=x*y*pow(z,2);
          A[60]+=x*z; A[61]+=pow(x,2)*z; A[62]+=x*y*z; A[63]+=x*pow(z,2); A[64]+=pow(x,3)*z; A[65]+=pow(x,2)*y*z; A[66]+=pow(x,2)*pow(z,2); A[67]+=x*pow(y,2)*z; A[68]+=x*y*pow(z,2); A[69]+=x*pow(z,3);
          A[70]+=pow(y,2); A[71]+=x*pow(y,2); A[72]+=pow(y,3); A[73]+=pow(y,2)*z; A[74]+=pow(x,2)*pow(y,2); A[75]+=x*pow(y,3); A[76]+=x*pow(y,2)*z; A[77]+=pow(y,4); A[78]+=pow(y,3)*z; A[79]+=pow(y,2)*pow(z,2);
          A[80]+=y*z; A[81]+=x*y*z; A[82]+=pow(y,2)*z; A[83]+=y*pow(z,2); A[84]+=pow(x,2)*y*z; A[85]+=x*pow(y,2)*z; A[86]+=x*y*pow(z,2); A[87]+=pow(y,3)*z; A[88]+=pow(y,2)*pow(z,2); A[89]+=y*pow(z,3);
          A[90]+=pow(z,2); A[91]+=x*pow(z,2); A[92]+=y*pow(z,2); A[93]+=pow(z,3); A[94]+=pow(x,2)*pow(z,2); A[95]+=x*y*pow(z,2); A[96]+=x*pow(z,3); A[97]+=pow(y,2)*pow(z,2); A[98]+=y*pow(z,3); A[99]+=pow(z,4);
          
          Eigen::Matrix<real_t, Eigen::Dynamic, 1> b = Eigen::Matrix<real_t, Eigen::Dynamic, 1>::Zero(10);
          
          b[0]+=psi[i]*1; b[1]+=psi[i]*x; b[2]+=psi[i]*y; b[3]+=psi[i]*z; b[4]+=psi[i]*pow(x,2); b[5]+=psi[i]*x*y; b[6]+=psi[i]*x*z; b[7]+=psi[i]*pow(y,2); b[8]+=psi[i]*y*z; b[9]+=psi[i]*pow(z,2);
          
          for(typename std::set<index_t>::const_iterator n=patch.begin(); n!=patch.end(); n++){
            x=_x[*n]; y=_y[*n]; z=_z[*n];
            
            A[0]+=1; A[1]+=x; A[2]+=y; A[3]+=z; A[4]+=pow(x,2); A[5]+=x*y; A[6]+=x*z; A[7]+=pow(y,2); A[8]+=y*z; A[9]+=pow(z,2);
            A[10]+=x; A[11]+=pow(x,2); A[12]+=x*y; A[13]+=x*z; A[14]+=pow(x,3); A[15]+=pow(x,2)*y; A[16]+=pow(x,2)*z; A[17]+=x*pow(y,2); A[18]+=x*y*z; A[19]+=x*pow(z,2);
            A[20]+=y; A[21]+=x*y; A[22]+=pow(y,2); A[23]+=y*z; A[24]+=pow(x,2)*y; A[25]+=x*pow(y,2); A[26]+=x*y*z; A[27]+=pow(y,3); A[28]+=pow(y,2)*z; A[29]+=y*pow(z,2);
            A[30]+=z; A[31]+=x*z; A[32]+=y*z; A[33]+=pow(z,2); A[34]+=pow(x,2)*z; A[35]+=x*y*z; A[36]+=x*pow(z,2); A[37]+=pow(y,2)*z; A[38]+=y*pow(z,2); A[39]+=pow(z,3);
            A[40]+=pow(x,2); A[41]+=pow(x,3); A[42]+=pow(x,2)*y; A[43]+=pow(x,2)*z; A[44]+=pow(x,4); A[45]+=pow(x,3)*y; A[46]+=pow(x,3)*z; A[47]+=pow(x,2)*pow(y,2); A[48]+=pow(x,2)*y*z; A[49]+=pow(x,2)*pow(z,2);
            A[50]+=x*y; A[51]+=pow(x,2)*y; A[52]+=x*pow(y,2); A[53]+=x*y*z; A[54]+=pow(x,3)*y; A[55]+=pow(x,2)*pow(y,2); A[56]+=pow(x,2)*y*z; A[57]+=x*pow(y,3); A[58]+=x*pow(y,2)*z; A[59]+=x*y*pow(z,2);
            A[60]+=x*z; A[61]+=pow(x,2)*z; A[62]+=x*y*z; A[63]+=x*pow(z,2); A[64]+=pow(x,3)*z; A[65]+=pow(x,2)*y*z; A[66]+=pow(x,2)*pow(z,2); A[67]+=x*pow(y,2)*z; A[68]+=x*y*pow(z,2); A[69]+=x*pow(z,3);
            A[70]+=pow(y,2); A[71]+=x*pow(y,2); A[72]+=pow(y,3); A[73]+=pow(y,2)*z; A[74]+=pow(x,2)*pow(y,2); A[75]+=x*pow(y,3); A[76]+=x*pow(y,2)*z; A[77]+=pow(y,4); A[78]+=pow(y,3)*z; A[79]+=pow(y,2)*pow(z,2);
            A[80]+=y*z; A[81]+=x*y*z; A[82]+=pow(y,2)*z; A[83]+=y*pow(z,2); A[84]+=pow(x,2)*y*z; A[85]+=x*pow(y,2)*z; A[86]+=x*y*pow(z,2); A[87]+=pow(y,3)*z; A[88]+=pow(y,2)*pow(z,2); A[89]+=y*pow(z,3);
            A[90]+=pow(z,2); A[91]+=x*pow(z,2); A[92]+=y*pow(z,2); A[93]+=pow(z,3); A[94]+=pow(x,2)*pow(z,2); A[95]+=x*y*pow(z,2); A[96]+=x*pow(z,3); A[97]+=pow(y,2)*pow(z,2); A[98]+=y*pow(z,3); A[99]+=pow(z,4);
            
            b[0]+=psi[*n]*1; b[1]+=psi[*n]*x; b[2]+=psi[*n]*y; b[3]+=psi[*n]*z; b[4]+=psi[*n]*pow(x,2); b[5]+=psi[*n]*x*y; b[6]+=psi[*n]*x*z; b[7]+=psi[*n]*pow(y,2); b[8]+=psi[*n]*y*z; b[9]+=psi[*n]*pow(z,2);
          }
          
          Eigen::Matrix<real_t, Eigen::Dynamic, 1> a = Eigen::Matrix<real_t, Eigen::Dynamic, 1>::Zero(10);
          A.lu().solve(b, &a);
          
          Hessian[i*9  ] = a[4]*2.0; // d2/dx2
          Hessian[i*9+1] = a[5];     // d2/dxdy
          Hessian[i*9+2] = a[6];     // d2/dxdz
          Hessian[i*9+3] = a[5];     // d2/dydx
          Hessian[i*9+4] = a[7]*2.0; // d2/dy2
          Hessian[i*9+5] = a[8];     // d2/dydz
          Hessian[i*9+6] = a[6];     // d2/dzdx
          Hessian[i*9+7] = a[8];     // d2/dzdy
          Hessian[i*9+8] = a[9]*2.0; // d2/dz2
        }
      }
    }
  }
  
  int _NNodes, _NElements, _ndims, nloc;
  const index_t *_ENList, *_node_distribution;
  const real_t *_x, *_y, *_z;
  std::vector< std::set<index_t> > NNList;
  std::vector<int> norder;
#ifdef HAVE_MPI
  const MPI_Comm *_mesh_comm;
#endif
  MetricTensor<real_t> *_metric;
  Surface<real_t, index_t> *_surface;
};

#endif