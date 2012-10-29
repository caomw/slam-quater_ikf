/**\file ikf.cpp
 *
 * This class has the primitive methods for an Indirect Kalman Filter implementation
 * for an Attitude and Heading Reference System - AHRS. The filter is Quaternion
 * based using accelerometers, gyroscopes and magnetometers. The filter performs the
 * prediction step based on the gyroscopes and therefore quaternion integration.
 * The measurement is formed by two step. First measurement step uses the accelerometers
 * in order to correct the pitch and roll angles. Second measurement step uses the
 * magnetometers only for the yaw angle. The first one estimates external acceleration
 * and compensate it increasing the measurement noise matrix.
 * 
 * This indirect Kalman filter is based on the paper:  Young Soo Suh, Member, IEEE
 * "Orientation estimation using a quaternion-based indirect Klaman filter with adaptive estimation of external acceleration"
 * A copy if the manuscript can be found in the /doc folder of the library.
 * 
 * @author Javier Hidalgo Carrio | DFKI RIC Bremen | javier.hidalgo_carrio@dfki.de
 * @date June 2011.
 * @version 1.0.
 */

#include <iostream> /**< IO C++ Standard library */
#include <algorithm> /**< Algorithm C++ Standard library */
#include <Eigen/LU> /**< Lineal algebra of Eigen */
#include <Eigen/SVD> /**< Singular Value Decomposition (SVD) of Eigen */
#include "ikf.h" /**< Indirect Kalman Filter */

namespace filter
{
  
    /** Namesapces to use **/
    using namespace Eigen;
    using namespace std;
    
    /** Indirect Kalman Filter methods **/
 
    /**
    * @brief This function Initilize the vectors and matrix of the IKF   
    */
    
    void ikf::Init(Eigen::Matrix <double,IKFSTATEVECTORSIZE,IKFSTATEVECTORSIZE> *P_0, Eigen::Matrix <double,NUMAXIS,NUMAXIS> *Ra, Eigen::Matrix <double,NUMAXIS,NUMAXIS> *Rg, Eigen::Matrix <double,NUMAXIS,NUMAXIS> *Rm,
		   Eigen::Matrix <double,NUMAXIS,NUMAXIS> *Qbg, Eigen::Matrix <double,NUMAXIS,NUMAXIS> *Qba, double g, double alpha)
    {
  
      /** Gravitation acceleration **/
      gtilde << 0, 0, g;

      /** Dip angle (alpha is in rad) **/
      mtilde(0) = cos(alpha);
      mtilde(1) = 0;
      mtilde(2) = -sin(alpha);


      /** Kalman filter state, error covariance and process noise covariance **/
      x = Matrix <double,IKFSTATEVECTORSIZE,1>::Zero();
      
      Q = Matrix <double,IKFSTATEVECTORSIZE,IKFSTATEVECTORSIZE>::Zero();            
      Q.block <NUMAXIS, NUMAXIS> (0,0) = 0.25 * (*Rg);
      Q.block <NUMAXIS, NUMAXIS> (3,3) = (*Qbg);
      Q.block <NUMAXIS, NUMAXIS> (6,6) = (*Qba);
      
      /** Initial error covariance **/
      P = (*P_0);
      
      H1 = Matrix <double,NUMAXIS,IKFSTATEVECTORSIZE>::Zero();
      H2 = Matrix <double,NUMAXIS,IKFSTATEVECTORSIZE>::Zero();
      H1(0,6) = 1; H1(1,7) = 1; H1(2,8) = 1;
      
      /** System matrix A **/
      A = Matrix <double,IKFSTATEVECTORSIZE,IKFSTATEVECTORSIZE>::Zero();      
      A(0,3) = -0.5;A(1,4) = -0.5;A(2,5) = -0.5;
      
      /** Initial measurement noise **/
      R = Matrix <double,NUMAXIS,NUMAXIS>::Zero();
      RHist = Eigen::Matrix <double,NUMAXIS,NUMAXIS*M1>::Zero();
      
      /** Initial bias **/
      bghat = Matrix <double,NUMAXIS,1>::Zero();
      bahat = Matrix <double,NUMAXIS,1>::Zero();
      
      /** Default omega matrix **/
      oldomega4 << 0 , 0 , 0 , 0,
	  0 , 0 , 0 , 0,
          0 , 0 , 0 , 0,
          0 , 0 , 0 , 0;
	  
	
      /** Initial quaternion in Init**/
      q4.w() = 1.00;
      q4.x() = 0.00;
      q4.y() = 0.00;
      q4.z() = 0.00;
      
      /** Default initial bias **/
      bghat << 0.00, 0.00, 0.00;
      bahat << 0.00, 0.00, 0.00;
      
	  
      /** Variable in the adaptive algorithm **/
      r1count = 0;
      r2count = R2COUNT;
      
      /** Fill matrix Rq, Ra and Rm **/
      ikf::Ra = (*Ra);
      ikf::Rg = (*Rg);
      ikf::Rm = (*Rm);
      
      /** Print filter information **/
//       std::cout<< "P:\n"<<P<<"\n";
//       std::cout<< "Q:\n"<<Q<<"\n";
//       std::cout<< "R:\n"<<R<<"\n";
//       std::cout<< "H1:\n"<<H1<<"\n";
//       std::cout<< "H2:\n"<<H2<<"\n";
//       std::cout<< "A:\n"<<A<<"\n";
//       std::cout<< "mtilde:\n"<<mtilde<<"\n";
//       std::cout<< "gtilde:\n"<<gtilde<<"\n";
//       std::cout<< "Ra:\n"<<(*Ra)<<"\n";
//       std::cout<< "Rg:\n"<<(*Rg)<<"\n";
//       std::cout<< "Rm:\n"<<(*Rm)<<"\n";

      return;      
    }
    
    /**
    * @brief This function Initilize Attitude
    */
    int ikf::setAttitude(Eigen::Quaternion< double > *initq)
    {
      if (initq != NULL)
      {
	/** Initial orientation **/
	q4 = (*initq);
	
	return OK;
      }
      
      return ERROR;
    }
    
    /**
    * @brief This function set the initial Omega matrix
    */
    int ikf::setOmega(Eigen::Matrix< double, NUMAXIS , 1  >* u)
    {
      if (u != NULL)
      {
	/** Initialization for quaternion integration **/
	oldomega4 << 0,-(*u)(0), -(*u)(1), -(*u)(2),
	  (*u)(0), 0, (*u)(2), -(*u)(1),
	  (*u)(1), -(*u)(2), 0, (*u)(0),
	  (*u)(2), (*u)(1), -(*u)(0), 0;
	 
	return OK;
      }
      return ERROR;
    }

    /**
    * @brief Gets the current orientation in Euler angles
    */
    Eigen::Matrix< double, NUMAXIS , 1  > ikf::getEuler()
    {
      Eigen::Matrix <double, NUMAXIS, 1> euler;
      
      //std::cout << Eigen::Matrix3d(q4) << std::endl; 
      Vector3d e = Eigen::Matrix3d(q4).eulerAngles(2,1,0);
       euler(0) = e[2]; 
       euler(1) = e[1]; 
       euler(2) = e[0]; 
//       std::cout << "Attitude (getEuler): "<< euler(0)<<" "<<euler(1)<<" "<<euler(2)<<"\n";
       //std::cout << "Attitude in degrees (getEuler): "<< euler(0)*R2D<<" "<<euler(1)*R2D<<" "<<euler(2)*R2D<<"\n";
      
      return euler;
    }
    
    /**
    * @brief Gets the current orientation in Quaternion
    */
    Eigen::Quaternion< double > ikf::getAttitude()
    {
      return q4;
    }

    /**
    * @brief Gets the current state vector of the filter
    */
    Eigen::Matrix< double, IKFSTATEVECTORSIZE , 1  > ikf::getState()
    {
      return x;

    }
    
     /**
    * @brief Set the current state vector of the filter
    */
    void ikf::setState(Eigen::Matrix< double, IKFSTATEVECTORSIZE , 1  > *x_0)
    {
      x = (*x_0);
      
      return;
    }
    
    
    /**
    * @brief Gets Noise covariance matrix
    */
    Eigen::Matrix< double, IKFSTATEVECTORSIZE , IKFSTATEVECTORSIZE> ikf::getCovariance()
    {
	return P;
    }

    
    /**
    * @brief Performs the prediction step of the filter.
    */
    void ikf::predict(Eigen::Matrix< double, NUMAXIS , 1  >* u, double dt)
    {
      Eigen::Matrix <double,NUMAXIS,NUMAXIS> vec2product; /**< Vec 2 product  matrix */
      Eigen::Matrix <double,NUMAXIS,1> angvelo; /**< Vec 2 product  matrix */
      Eigen::Matrix <double,QUATERSIZE,QUATERSIZE> omega4; /**< Quaternion integration matrix */
      Eigen::Matrix <double,QUATERSIZE,1> quat; /**< Quaternion integration matrix */
      Eigen::Matrix <double,IKFSTATEVECTORSIZE,IKFSTATEVECTORSIZE> dA; /**< Discrete System matrix */
      Eigen::Matrix <double,IKFSTATEVECTORSIZE,IKFSTATEVECTORSIZE> Qd; /**< Discrete Q matrix */
      
      
      /** Compute the vector2product matrix with the angular velocity **/
      angvelo = (*u) - bghat; /** Eliminate the Bias **/
      
      vec2product << 0, -angvelo(2), angvelo(1),
		    angvelo(2), 0, -angvelo(0),
		    -angvelo(1), angvelo(0), 0;
		 
      /** Compute the dA Matrix **/
      A.block<NUMAXIS, NUMAXIS> (0,0) = -vec2product;
      dA = Matrix<double,IKFSTATEVECTORSIZE,IKFSTATEVECTORSIZE>::Identity() + A * dt + A * A * pow(dt,2)/2;
      
      /** Propagate the vector through the system **/
      x = dA * x;
      Qd = Q*dt + 0.5*dt*dt*A*Q + 0.5 *dt*dt*Q*A.transpose();
      Qd = 0.5*(Qd + Qd.transpose());
      P = dA*P*dA.transpose() + Qd;
      
      omega4 << 0,-angvelo(0), -angvelo(1), -angvelo(2),
		angvelo(0), 0, angvelo(2), -angvelo(1),
		angvelo(1), -angvelo(2), 0, angvelo(0),
		angvelo(2), angvelo(1), -angvelo(0), 0;
	
      quat(0) = q4.w();
      quat(1) = q4.x();
      quat(2) = q4.y();
      quat(3) = q4.z();
      
      
      quat = (Matrix<double,QUATERSIZE,QUATERSIZE>::Identity() +(0.75 * omega4 *dt)- (0.25 * oldomega4 * dt) -
      ((1/6) * angvelo.squaredNorm() * pow(dt,2) *  Matrix<double,QUATERSIZE,QUATERSIZE>::Identity()) -
      ((1/24) * omega4 * oldomega4 * pow(dt,2)) - ((1/48) * angvelo.squaredNorm() * omega4 * pow(dt,3))) * quat;
    
      q4.w() = quat(0);
      q4.x() = quat(1);
      q4.y() = quat(2);
      q4.z() = quat(3);
      q4.normalize();
      
      oldomega4 = omega4;
      
      return;

    }
    
    
     /**
    * @brief Performs the measurement and correction steps of the filter.
    */
    void ikf::update(Eigen::Matrix< double, NUMAXIS , 1  >* acc, Eigen::Matrix< double, NUMAXIS , 1  >* mag, bool magn_on_off)
    {
      register int j;
      Eigen::Matrix <double,NUMAXIS,NUMAXIS> Cq; /**< Rotational matrix */
      Eigen::Matrix <double,NUMAXIS,NUMAXIS> vec2product; /**< Vec 2 product  matrix */
      Eigen::Matrix <double,NUMAXIS,NUMAXIS> fooR2; /**<  Measurement noise matrix from accelerometers matrix Ra*/
      Eigen::Matrix <double,IKFSTATEVECTORSIZE,IKFSTATEVECTORSIZE> P1; /**< Error convariance matrix for measurement 1*/
      Eigen::Matrix <double,IKFSTATEVECTORSIZE,IKFSTATEVECTORSIZE> P2; /**< Error convariance matrix for measurement 2*/
      Eigen::Matrix <double,IKFSTATEVECTORSIZE,IKFSTATEVECTORSIZE> auxM; /**< Auxiliar matrix for computing Kalman gain in measurement*/
      Eigen::Matrix <double,IKFSTATEVECTORSIZE, NUMAXIS> K1; /**< Kalman Gain matrix for measurement 1*/
      Eigen::Matrix <double,IKFSTATEVECTORSIZE, NUMAXIS> K2; /**< Kalman Gain matrix for measurement 2*/
      Eigen::Matrix <double,NUMAXIS,NUMAXIS> Uk; /**< Uk measurement noise convariance matrix for the adaptive algorithm */
      Eigen::Matrix <double,NUMAXIS,NUMAXIS> Qstar; /**< External acceleration covariance matrix */
      Eigen::Quaternion <double> qe;  /**< Attitude error quaternion */
      Eigen::Matrix <double,NUMAXIS,1> gtilde_body; /**< Gravitation in the body frame */
      Eigen::Matrix <double,NUMAXIS,1> mtilde_body; /**< Magnetic field in the body frame */
      Eigen::Matrix <double,NUMAXIS,NUMAXIS> u; /**< Unitary matrix U for the SVD decomposition */
      Eigen::Matrix <double,NUMAXIS,1> s; /**< Unitary matrix V for the SVD decomposition */
      Eigen::Matrix <double,NUMAXIS,1> lambda; /**< Lambda vector for the adaptive algorithm */
      Eigen::Matrix <double,NUMAXIS,1> mu; /**< mu vector for the adaptive algorithm */
      Eigen::Matrix <double,NUMAXIS,1> z1; /**< Measurement vector 1 Acc */
      Eigen::Matrix <double,NUMAXIS,1> z2; /**< Measurement vector 2 Mag */
      Eigen::Matrix <double,NUMAXIS,1> auxvector; /**< Auxiliar vector variable */
      //Eigen::Matrix <double,NUMAXIS,1> auxvector2; /**< Measurement vector 1 */
      
      auxvector << 0, 0, 1;
	    
      /**----------------------- **/
      /** Measurement step 1 Acc **/
      /**----------------------- **/
      
      /** Create the orientation matrix from the quaternion **/
      Quaternion2DCM (&q4, &Cq);
      
      /** First measurement step (Pitch and Roll correction from Acc) **/
      gtilde_body = Cq * gtilde;
      vec2product << 0, -gtilde_body(2), gtilde_body(1),
		    gtilde_body(2), 0, -gtilde_body(0),
		    -gtilde_body(1), gtilde_body(0), 0;
		    
      H1.block<NUMAXIS, NUMAXIS> (0,0) = 2*vec2product;
      
      /** The adaptive algorithm, the Uk matrix and SVD part **/
      z1 = (*acc) - bahat - gtilde_body;
      R = (z1 - H1*x) * (z1 - H1*x).transpose();
      RHist.block <NUMAXIS, NUMAXIS> (0, (r1count*(M1-1))%M1) = R;
      
      /** r1count + 1 modulus the number of history M1 **/
      r1count++; 

      
      /** Starting the Uk is R **/
      Uk = R;
      for (j=0; j<M1; j++)
      {
	Uk = Uk + RHist.block <NUMAXIS, NUMAXIS> (0,NUMAXIS*j);
      }
      
        Uk = Uk / (M1);
      
      
      fooR2 = H1*P*H1.transpose() + Ra;
      
      /**
       * Single Value Decomposition
       */
      JacobiSVD <MatrixXd > svdOfR(Uk, ComputeThinU);

      s = svdOfR.singularValues();
      u = svdOfR.matrixU();
      
      lambda << s(0), s(1), s(2);
     
      mu(0) = (u.transpose().row(0) * fooR2).dot(u.col(0));
      mu(1) = (u.transpose().row(1) * fooR2).dot(u.col(1));
      mu(2) = (u.transpose().row(2) * fooR2).dot(u.col(2));
      
      if ((lambda - mu).maxCoeff() > GAMMA)
      {
	r2count = 0;
	auxvector(0) = max(lambda(0)-mu(0),(double)0.00);
	auxvector(1) = max(lambda(1)-mu(1),(double)0.00);
	auxvector(2) = max(lambda(2)-mu(2),(double)0.00);
	
	Qstar = auxvector(0) * u.col(0) * u.col(0).transpose() + auxvector(1) * u.col(1) * u.col(1).transpose() + auxvector(2) * u.col(2) * u.col(2).transpose();
      }
      else
      {
	r2count ++;
	if (r2count < M2)
	  Qstar = auxvector(0) * u.col(0) * u.col(0).transpose() + auxvector(1) * u.col(1) * u.col(1).transpose() + auxvector(2) * u.col(2) * u.col(2).transpose();
	else
	  Qstar = Matrix<double, NUMAXIS, NUMAXIS>::Zero();
      }
      
      /** Compute the Kalman Gain Matrix **/
      if (magn_on_off == false)
      {
	P1 = Matrix<double, IKFSTATEVECTORSIZE, IKFSTATEVECTORSIZE>::Zero();
	P1.block<NUMAXIS, NUMAXIS>(0,0) = P.block<NUMAXIS, NUMAXIS>(0,0);
	
	auxvector << 1, 1, 0;
	auxvector = Cq * auxvector;

	auxM = Matrix<double, IKFSTATEVECTORSIZE, IKFSTATEVECTORSIZE>::Zero();
	auxM.block<NUMAXIS, NUMAXIS>(0,0) = auxvector * auxvector.transpose();
	K1 = auxM * P1 * H1.transpose() * (H1*P1*H1.transpose() + Ra + Qstar).inverse();
      }
      else
      {
	P1 = P;
	K1 = P1 * H1.transpose() * (H1 * P1 * H1.transpose() + Ra + Qstar).inverse(); //Qstart is the external acceleration covariance
      }
      
	
//       std::cout<< "K1:\n"<<K1<<"\n";
	
      /** Update the state vector and the covariance matrix **/
      x = x + K1 * (z1 - H1 * x);
      P = (Matrix<double,IKFSTATEVECTORSIZE,IKFSTATEVECTORSIZE>::Identity()-K1*H1)*P*(Matrix<double,IKFSTATEVECTORSIZE,IKFSTATEVECTORSIZE>::Identity()-K1*H1).transpose() + K1*(Ra+Qstar)*K1.transpose();
      
      P = 0.5 * (P + P.transpose());
      
      /** Update the quaternion with the Indirect approach **/
      /** This is necessary mainly because after(in the 2 measurement) C(q) is computed **/
      qe.w() = 1;
      qe.x() = x(0);
      qe.y() = x(1);
      qe.z() = x(2);
      q4 = q4 * qe;
      
      /** Normalize quaternion **/
      q4.normalize();

      
      /** Reset the quaternion part of the state vector **/
      x.block<NUMAXIS,1>(0,0) = Matrix<double, NUMAXIS, 1>::Zero();
      
      
      /**------------------------- **/
      /** Measurement step 2 Mag   **/
      /** It only updates Yaw angle**/
      /**------------------------- **/
      
      if (magn_on_off == true)
      {
    
	/** Create the orientation matrix from the quaternion **/
	Quaternion2DCM (&q4, &Cq);


	/** Second measurement step **/
	mtilde_body = Cq * mtilde;
	vec2product << 0, -mtilde_body(2), mtilde_body(1),
		    mtilde_body(2), 0, -mtilde_body(0),
		    -mtilde_body(1), mtilde_body(0), 0;
		    
	/** Observation matrix **/
	H2.block<NUMAXIS, NUMAXIS> (0,0) = 2*vec2product;

	/** Measurement vector **/
	z2 = (*mag) - mtilde_body;

	P2 = Matrix<double, IKFSTATEVECTORSIZE, IKFSTATEVECTORSIZE>::Zero();
	P2.block<NUMAXIS, NUMAXIS>(0,0) = P.block<NUMAXIS, NUMAXIS>(0,0);

	auxvector << 0, 0, 1;
	auxvector = Cq * auxvector;

	/** Compute Kalman Gain **/
	auxM = Matrix<double, IKFSTATEVECTORSIZE, IKFSTATEVECTORSIZE>::Zero();
	auxM.block<NUMAXIS, NUMAXIS>(0,0) = auxvector * auxvector.transpose();
	K2 = auxM * P2 * H2.transpose() * (H2*P2*H2.transpose() + Rm).inverse();

	/** Update the state vector and the covariance matrix **/
	x = x + K2*(z2 - (H2*x));
	P = P - K2 * H2 * P - P * H2.transpose() * K2.transpose() + K2*(H2*P*H2.transpose() + Rm)*K2.transpose();
	P = 0.5 * (P + P.transpose());

	/** Update the quaternion with the Indirect approach **/
	qe.w() = 1;
	qe.x() = x(0);
	qe.y() = x(1);
	qe.z() = x(2);
	q4 = q4 * qe;

	/** Normalize quaternion **/
	q4.normalize();

	/** Reset the quaternion part of the state vector **/
	x.block<NUMAXIS,1>(0,0) = Matrix<double, NUMAXIS, 1>::Zero();
      }
      
      /**---------------------------- **/
      /** Reset the rest of the state **/
      /**---------------------------- **/
      bghat = bghat + x.block<NUMAXIS, 1> (3,0);
      x.block<NUMAXIS, 1> (3,0) = Matrix <double, NUMAXIS, 1>::Zero();
      
      bahat = bahat + x.block<NUMAXIS, 1> (6,0);
      x.block<NUMAXIS, 1> (6,0) = Matrix <double, NUMAXIS, 1>::Zero();
      
      return;
    }

       
    /**
    * @brief Conversion Quaternion to DCM (Direct Cosine Matrix) (Alternative to Eigen)
    */
    void ikf::Quaternion2DCM(Eigen::Quaternion< double >* q, Eigen::Matrix< double, NUMAXIS, NUMAXIS  >*C)
    {
      double q0, q1, q2, q3;

      if (C != NULL)
      {
	/** Take the parameters of the quaternion */
	q0 = q->w();
	q1 = q->x();
	q2 = q->y();
	q3 = q->z();
	
	/** Create the DCM matrix from the actual quaternion */
	(*C)(0,0) = 2 * q0 * q0 + 2 * q1 * q1 - 1;
	(*C)(0,1) = 2 * q1 * q2 + 2 * q0 * q3;
	(*C)(0,2) = 2 * q1 * q3 - 2 * q0 * q2;
	(*C)(1,0) = 2 * q1 * q2 - 2 * q0 * q3;
	(*C)(1,1) = 2 * q0 * q0 + 2 * q2 * q2 - 1;
	(*C)(1,2) = 2 * q2 * q3 + 2 * q0 * q1;
	(*C)(2,0) = 2 * q1 * q3 + 2 * q0 * q2;
	(*C)(2,1) = 2 * q2 * q3 - 2 * q0 * q1;
	(*C)(2,2) = 2 * q0 * q0 + 2 * q3 * q3 - 1;	
      }
      
      return;
    }

}
