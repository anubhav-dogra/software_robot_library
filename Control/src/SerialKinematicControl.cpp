#include <SerialKinematicControl.h>
 
  ////////////////////////////////////////////////////////////////////////////////////////////////////
 //                                        Constructor                                             //
////////////////////////////////////////////////////////////////////////////////////////////////////
SerialKinematicControl::SerialKinematicControl(const std::vector<RigidBody> &links,
                                               const std::vector<Joint>     &joints,
                                               const float &controlFrequency):
                                               SerialControlBase(links,joints,controlFrequency)
{

/* NOTE: This should be all in the SerialControlBase class.

	// Not sure if I should "duplicate" the joint limits here, or just grab them from the Joint class... 
	// It makes the code a little neater, and the limits in the Control class can be altered
	
	// Resize vectors
	this->pLim.resize(this->n);
	this->vLim.resize(this->n);
	this->aLim.resize(this->n);
	
	// Transfer the values from the Joint classes
	for(int i = 0; i < this->n; i++)
	{
		this->joint[i].get_position_limits(this->pLim[i][0], this->pLim[i][1]);
		this->vLim[i] = this->joint[i].get_velocity_limit();
		this->aLim[i] = 5.0;
	}
*/
}

  ////////////////////////////////////////////////////////////////////////////////////////////////////
 //                        Set the gain used for proportional feedback control                     //
////////////////////////////////////////////////////////////////////////////////////////////////////
bool SerialKinematicControl::set_proportional_gain(const float &gain)
{
	if(gain == 0)
	{
		std::cerr << "[ERROR] [SERIALKINEMATICCONTROL] set_proportional_gain(): "
		          << "Value cannot be zero. Gain not set." << std::endl;
		          
		return false;
	}
	else if(gain < 0)
	{
		std::cerr << "[WARNING] [SERIALKINEMATICCONTROL] set_proportional_gain(): "
		          << "Gain of " << gain << " cannot be negative. "
		          << "It has been automatically made positive..." << std::endl;
                         
		this->k = -1*gain;
		return true;
	}
	else
	{
		this->k = gain;
		return true;
	}
}

  ////////////////////////////////////////////////////////////////////////////////////////////////////
 //                       Get the error between two poses for feedback control                     //
////////////////////////////////////////////////////////////////////////////////////////////////////
Eigen::Matrix<float,6,1> SerialKinematicControl::get_pose_error(const Eigen::Isometry3f &desired,
                                                                const Eigen::Isometry3f &actual)
{
	// Yuan, J. S. (1988). Closed-loop manipulator control using quaternion feedback.
	// IEEE Journal on Robotics and Automation, 4(4), 434-440.
	
	Eigen::Matrix<float,6,1> error;
	error.head(3) = desired.translation() - actual.translation();                               // Difference in translation
	
	// Convert rotation components to quaternion
	Eigen::Quaternionf qd(desired.rotation());
	Eigen::Quaternionf qa(actual.rotation());
	Eigen::Quaternionf qe = qd*qa.conjugate();
	
	if(qd.dot(qa) <= 0) error.tail(3) = qe.vec();                                               // Angle between quaternions <= 90 degrees
	else                error.tail(3) =-qe.vec();                                               // Angle greater than 90 degrees, go other direction 
	
	return error;
}

  ///////////////////////////////////////////////////////////////////////////////////////////////////
 //                               Move the endpoint at a given speed                              //
///////////////////////////////////////////////////////////////////////////////////////////////////
Eigen::VectorXf SerialKinematicControl::move_at_speed(const Eigen::Matrix<float,6,1> &vel)
{
	if(this->n <= 6) return move_at_speed(vel, Eigen::VectorXf::Zero(this->n));
	else		 return move_at_speed(vel, singularity_avoidance(0.5));
}

  ///////////////////////////////////////////////////////////////////////////////////////////////////
 //                             Move the endpoint at a given speed                                //
///////////////////////////////////////////////////////////////////////////////////////////////////
Eigen::VectorXf SerialKinematicControl::move_at_speed(const Eigen::Matrix<float,6,1> &vel,
                                                      const Eigen::VectorXf          &redundant)
{
	// Whitney, D. E. (1969). Resolved motion rate control of manipulators and human prostheses.
	// IEEE Transactions on man-machine systems, 10(2), 47-53.
	//
	// Given xdot = J*qdot, we can solve for:
	//
	// - qdot = J^-1 *xdot               if n <= 6
	// - qdot = J^-1 &xdot + N*redundant otherwise
	//
	// Matrix inversion is prone to numerical instability when ill-conditioned,
	// so we can use QR decomposition instead
	
	if(redundant.size() != this->n)
	{
		std::cerr << "[ERROR] [SERIALKINEMATICCONTROL] move_at_speed(): "
		          << "Expected a " << this->n << "x1 vector for the redundant task, "
		          << "but it was " << redundant.size() << "x1." << std::endl;
		
		return 0.9*get_joint_velocities();
	}
	else
	{
		Eigen::MatrixXf J = get_jacobian();                                                 // As it says on the label
		
		Eigen::VectorXf xMin(this->n), xMax(this->n);
		for(int i = 0; i < this->n; i++) get_speed_limit(xMin(i),xMax(i),i);                // Compute instantaneous speed limits
		
		if(this->n <= 6)
		{
			return least_squares(vel,
			                     J,
			                     Eigen::MatrixXf::Identity(this->n,this->n),
			                     xMin,
			                     xMax,
			                     0.5*(xMin + xMax));                                    // Too easy, lol
/*
			//    J*qdot = xdot
			// J'*J*qdot = J'*xdot
			//  Q*R*qdot = J'*xdot
			//    R*qdot = Q'*J'*xdot
	
			Eigen::MatrixXf Q,R;
			if(get_qr_decomposition(J.transpose()*J,Q,R))
			{
				return solve_joint_control(Q.transpose()*J.transpose()*vel, R);
			}
			else
			{
				std::cerr << "[ERROR] [SerialKinematicControl] move_at_speed(): "
				          << "Something went wrong with QR decomposition. "
				          << "Could not solve the joint control." << std::endl;
				
				return 0.9*get_joint_velocities();                                  // Slow down current joint velocities
			}
*/
		}
		else // this->n > 6
		{
			Eigen::MatrixXf W = get_inertia();                                          // Weight by inertia for minimum energy
			for(int i = 0; i < this->n; i++) W(i,i) += get_joint_penalty(i) - 1;        // Penalise motion toward joints
			
			return least_squares(redundant,W,vel,J,xMin,xMax,get_joint_velocities());   // Solve with constrained QP
/*
			// Solve min 0.5*(qdot_0 - qdot)'*W*(qdot_0 - qdot) s.t. J*qdot = xdot
			//
			// Lagrangian L = 0.5*qdot'*W*qdot - qdot'*W*qdot_0 + (J*qdot - xdot)'*lambda
			//
			// Solution exists where partial derivative is zero:
			//
			// [ dL/dlambda] = [ 0   J ][ lambda ] - [   xdot   ] = [ 0 ]
			// [ dL/dqdot  ]   [ J'  W ][  qdot  ]   [ W*qdot_0 ]   [ 0 ]
			//  [ Q11  Q12 ][ R12  R12 ][ lambda ] - [   xdot   ] = [ 0 ]
			//  [ Q21  Q22 ][ R21  R22 ][  qdot  ]   [ W*qdot_0 ]   [ 0 ]
			
			Eigen::MatrixXf W = get_inertia();                                          // Weight by inertia for minimum effort
			for(int i = 0; i < this->n; i++) W(i,i) += get_joint_penalty(i) - 1;        // Penalise motion toward a joint
			 
			Eigen::MatrixXf H(6+this->n,6+this->n);
			H.block(0,0,      6,      6).setZero();
			H.block(6,0,this->n,      6) = J.transpose();
			H.block(0,6,      6,this->n) = J;
			H.block(6,6,this->n,this->n) = W;
			
			Eigen::MatrixXf Q, R;
			if(get_qr_decomposition(H,Q,R))
			{
				// Note: We don't actually need to solve for lambda
				return solve_joint_control(Q.block(0,6,      6,this->n).transpose()*vel
				                         + Q.block(6,6,this->n,this->n).transpose()*W*qdot,
				                           R.block(6,6,this->n,this->n));
			}
			else
			{
				std::cerr << "[ERROR] [SerialKinematicControl] move_at_speed(): "
				          << "Something went wrong with QR decomposition. "
				          << "Could not solve the joint control." << std::endl;
				
				return 0.9*get_joint_velocities();                                  // Slow down current joint velocities
			}
*/
		}
	}
}

  ///////////////////////////////////////////////////////////////////////////////////////////////////
 //                Solve the joint velocities from an upper-triangular matrix                     //
///////////////////////////////////////////////////////////////////////////////////////////////////
Eigen::VectorXf SerialKinematicControl::solve_joint_control(const Eigen::VectorXf &y,
                                                            const Eigen::MatrixXf &U)
{
	if(y.size() != this->n)
	{
		std::cerr << "[ERROR] [SERIALKINEMATICCONTROL] solve_joint_control(): "
		          << "Expected a " << this->n << "x1 vector for the input, "
		          << "but it was " << y.size() << "x1." << std::endl;
		
		return 0.9*get_joint_velocities();
	}
	else if(U.rows() != this->n or U.cols() != this->n)
	{
		std::cerr << "[ERROR] [SERIALKINEMATICCONTROL] solve_joint_control(): "
		          << "Expected a " << this->n << "x" << this->n << " matrix for the input, "
		          << "but it was " << U.rows() << "x" << U.cols() << "." << std::endl;
		
		return 0.9*get_joint_velocities();
	}
	else
	{
		Eigen::VectorXf qdot(this->n);                                                      // Value to be returned
		
		for(int i = this->n-1; i >= 0; i--)                                                 // Start from last joint and solve backwards
		{
			float sum = 0.0;
			for(int j = i+1; j < this->n; j++)
			{
				sum += U(i,j)*qdot(j);                                              // Sum up recursive values
			}
			
			if(abs(U(i,i)) < 1E-5) qdot(i) = 0.9*get_joint_velocity(i);                 // Singular; slow down current joint speed
			else                   qdot(i) = (y(i) - sum)/U(i,i);
		
			// Obey joint limits
			float lower, upper;
			get_speed_limit(upper, lower, i);
			
			if     (qdot(i) < lower) qdot(i) = lower;
			else if(qdot(i) > upper) qdot(i) = upper;
		}
		
		return qdot;
	}
}

  ////////////////////////////////////////////////////////////////////////////////////////////////////
 //            Get the joint velocity to move to a given joint position at max. speed              //
////////////////////////////////////////////////////////////////////////////////////////////////////
Eigen::VectorXf SerialKinematicControl::move_to_position(const Eigen::VectorXf &pos)
{
	if(pos.size() != this->n)
	{
		std::cerr << "[ERROR] [SERIALKINEMATICCONTROL] move_to_position(): "
		          << "Expected a " << this->n << "x1 vector for the input, "
		          << "but it was " << pos.size() << "x1." << std::endl;
		
		return 0.9*get_joint_velocities();                                                  // Slow down joints to avoid problems
	}
	else
	{
		Eigen::VectorXf ctrl(this->n);                                                      // Value to be returned
		
		for(int i = 0; i < this->n; i++)
		{
			ctrl(i) = this->k*(pos(i) - this->q[i]);                                    // Use proportional feedback
			
			// Limit the speed
			float lower, upper;
			get_speed_limit(lower, upper, i);
			if     (ctrl(i) < lower) ctrl(i) = lower;
			else if(ctrl(i) > upper) ctrl(i) = upper;
		}
		
		return ctrl;
	}
}

  ////////////////////////////////////////////////////////////////////////////////////////////////////
 //                            Move the robot to a given endpoint pose                             //
////////////////////////////////////////////////////////////////////////////////////////////////////
Eigen::VectorXf SerialKinematicControl::move_to_pose(const Eigen::Isometry3f &pose)
{
	return move_at_speed(this->k*get_pose_error(pose,get_endpoint_pose()));                     // Proportional feedback
}

  ////////////////////////////////////////////////////////////////////////////////////////////////////
 //                            Move the robot to a given endpoint pose                             //
////////////////////////////////////////////////////////////////////////////////////////////////////
Eigen::VectorXf SerialKinematicControl::move_to_pose(const Eigen::Isometry3f &pose,
                                                     const Eigen::VectorXf   &redundancy)
{
	if(redundancy.size() != this->n)
	{
		std::cerr << "[ERROR] [SERIALKINEMATICCONTROL] move_to_pose(): "
		          << "Expected a " << this->n << "x1 vector for the redundant task, "
		          << "but it was " << redundancy.size() << "x1." << std::endl;
		
		return 0.9*get_joint_velocities();                                                  // Slow down to avoid problems
	}
	else
	{
		return move_at_speed(this->k*get_pose_error(pose,get_endpoint_pose()),redundancy);
	}
}

  ////////////////////////////////////////////////////////////////////////////////////////////////////
 //                                   Track Cartesian trajectory                                   //
////////////////////////////////////////////////////////////////////////////////////////////////////
Eigen::VectorXf SerialKinematicControl::track_cartesian_trajectory(const Eigen::Isometry3f        &pose,
                                                                   const Eigen::Matrix<float,6,1> &vel)
{
	return move_at_speed(vel + this->k*get_pose_error(pose,get_endpoint_pose()));               // Feedforward + feedback control
}

  ////////////////////////////////////////////////////////////////////////////////////////////////////
 //                                   Track Cartesian trajectory                                   //
////////////////////////////////////////////////////////////////////////////////////////////////////
Eigen::VectorXf SerialKinematicControl::track_cartesian_trajectory(const Eigen::Isometry3f        &pose,
                                                                   const Eigen::Matrix<float,6,1> &vel,
                                                                   const Eigen::VectorXf          &redundant)
{
	if(redundant.size() != this->n)
	{
		std::cerr << "[ERROR] [SERIALKINEMATICCONTROL] track_cartesian_trajectory(): "
		          << "Expected a " << this->n << "x1 vector for the redundant task, "
		          << "but it was " << redundant.size() << "x1." << std::endl;
		
		return 0.9*get_joint_velocities();
	}
	else
	{
		return move_at_speed(vel + this->k*(get_pose_error(pose,get_endpoint_pose())), redundant);
	}
}

  ////////////////////////////////////////////////////////////////////////////////////////////////////
 //                                   Track a joint trajectory                                     //
////////////////////////////////////////////////////////////////////////////////////////////////////
Eigen::VectorXf SerialKinematicControl::track_joint_trajectory(const Eigen::VectorXf &pos,
                                                               const Eigen::VectorXf &vel)
{
	if(pos.size() != this->n or vel.size() != this->n)
	{
		std::cerr << "[ERROR] [SERIALKINEMATICCONTROL] track_joint_trajectory(): "
		          << "This robot has " << this->n << " joints, but "
		          << "position vector was " << pos.size() << "x1 and "
		          << "velocity vector was " << vel.size() << "x1." << std::endl;
		
		return 0.9*get_joint_velocities();
	}
	else
	{
		Eigen::VectorXf ctrl(this->n);                                                      // Value to be returned
		
		for(int i = 0; i < this->n; i++)
		{
			ctrl(i) = vel(i) + this->k*(pos(i) - get_joint_position(i));                // Feedforward + feedback control
			
			// Limit the joint speeds
			float lower, upper;
			get_speed_limit(lower, upper, i);
			
			if     (ctrl(i) < lower) ctrl(i) = lower;
			else if(ctrl(i) > upper) ctrl(i) = upper;
		}
		
		return ctrl;
	}
}

  ////////////////////////////////////////////////////////////////////////////////////////////////////
 //                 Get the speed limits on a particular joint for the current state               //
////////////////////////////////////////////////////////////////////////////////////////////////////
bool SerialKinematicControl::get_speed_limit(float &lower, float &upper, const int &i)
{
	// Flacco, F., De Luca, A., & Khatib, O. (2012, May). Motion control of redundant robots
	// under joint constraints: Saturation in the null space.
	// In 2012 IEEE International Conference on Robotics and Automation (pp. 285-292). IEEE.
	
	// Compute lowest possible speed
	float p = (this->pLim[i][0] - this->q[i])/this->dt;                                         // Minimum speed before hitting joint limit
	float v = -this->vLim[i];                                                                   // Negative of maximum motor speed
	float a = -2*sqrt(this->aLim[i]*(this->q[i] - this->pLim[i][0]));                           // Minimum speed at maximum braking
	
	lower = std::max(p,std::max(v,a));                                                          // Return largest of the 3
	
	// Compute highest possible speed
	p = (this->pLim[i][1] - this->q[i])/this->dt;                                               // Maximum speed before hitting joint limit
	v = this->vLim[i];                                                                          // Maximum motor speed
	a = 2*sqrt(this->aLim[i]*(this->pLim[i][1] - this->q[i]));                                  // Maximum speed at maximum braking
	
	upper = std::min(p,std::min(v,a));                                                          // Return smallest
	
	return true;
}
  ////////////////////////////////////////////////////////////////////////////////////////////////////
 //                           Get penalty term for joint limit avoidance                           //
////////////////////////////////////////////////////////////////////////////////////////////////////
float SerialKinematicControl::get_joint_penalty(const int &i)
{
	// Chan, T. F., & Dubey, R. V. (1995). A weighted least-norm solution based scheme
	// for avoiding joint limits for redundant joint manipulators.
	// IEEE Transactions on Robotics and Automation, 11(2), 286-292.
	
	// NOTE: Minimum of penalty function is 1, so subtract 1 if you want 0 penalty at midpoint
	
	float q     = get_joint_position(i);                                                        // Position of ith joint
	float lower = q - this->pLim[i][0];                                                         // Distance from lower limit
	float upper = this->pLim[i][1] - q;                                                         // Distance to upper limit
	float range = this->pLim[i][1] - this->pLim[i][0];                                          // Distance between upper and lower
	float dpdq  = (range*range*(2*q - this->pLim[i][1] - this->pLim[i][0]))                     // Partial derivative of penalty function
	             /(4*upper*upper*lower*lower);

	if(dpdq*get_joint_velocity(i) > 0.0)                                                        // If moving toward a limit...
	{
		return range*range/(4*upper*lower);                                                 // Penalty term
	}
	else	return 1.0;                                                                         // Don't penalise if moving away
}

  ////////////////////////////////////////////////////////////////////////////////////////////////////
 //               Compute the gradient of manipulability to avoid singularities                    //
////////////////////////////////////////////////////////////////////////////////////////////////////
Eigen::VectorXf SerialKinematicControl::singularity_avoidance(const float &scalar)
{
	// Yoshikawa, T. (1985). Manipulability of robotic mechanisms.
	// The international journal of Robotics Research, 4(2), 3-9.
	
	// Proof of the partial derivative can be found in this book:
	// Marani, G., & Yuh, J. (2014). Introduction to autonomous manipulation:
	// Case Study with an Underwater Robot, SAUVIM
	// Volume 102 of Springer Tracts in Advanced Robotics. Springer.
	
	if(scalar <= 0)
	{
		std::cerr << "[ERROR] [SERIALKINEMATICCONTROL] singularity_avoidance(): "
			  << "Input argument was " << scalar << " but it must be positive!" << std::endl;
	
		return Eigen::VectorXf::Zero(this->n);
	}
	else
	{
		Eigen::MatrixXf J    = get_jacobian();                                              // As it says on the label
		Eigen::MatrixXf invJ = get_pseudoinverse(J);                                        // As it says on the label
		
		float mu = sqrt((J*J.transpose()).determinant());                                   // Actual measure of manipulability
		
		Eigen::MatrixXf dJ;                                                                 // Placeholder for partial derivative
		
		Eigen::VectorXf grad(this->n);                                                      // Value to be returned
		
		grad(0) = 0.0;                                                                      // First joint does not affect manipulability
		
		for(int i = 1; i < this->n; i++)
		{
			dJ = get_partial_derivative(J,i);                                           // Get partial derivative w.r.t ith joint
			grad(i) = scalar*mu*(dJ*invJ).trace();                                      // Gradient of manipulability
		}
		
		return grad;
	}
}


  //////////////////////////////////////////////////////////////////////////////////////////////////////
 //                           Move the endpoint at the given velocity                                //
//////////////////////////////////////////////////////////////////////////////////////////////////////
Eigen::VectorXf move_endpoint(const Eigen::Matrix<float,6,1> &speed)
{
	Eigen::VectorXf vel(this->n);                                                               // Value to be returned
	Eigen::VectorXf startPoint(this->n);
	
	// Compute instantaneous lower and upper bound on joint velocity
	Eigen::VectorXf lowerBound(this->n), upperBound(this->n);
	for(int i = 0; i < this->n; i++)
	{
		get_velocity_bounds(lowerBound[i], upperBound[i],i);
		startPoint(i) = 0.5*(lowerBound[i], upperBound[i]);                                 // Halfway point
	}
	
	if(this->n <= 6)
	{
		return least_squares(speed, this->J, Eigen::MatrixXf::Identity(this->n,this->n), lowerBound, upperBound, startPoint);
	}
	else
	{
		// Solve the redundant task
		Eigen::VectorXf redundancy;
		if(this->redundantTaskSet) redundancy = this->redundantTask;
		else                       redundancy = this->kd*(avoid_singularities() - this->qdot);
		this->redundantTaskSet = false;                                                             // User must reset the task
		
		Eigen::MatrixXf M = get_inertia_matrix();
		
		return least_squares(redundancy, M, J, speed, lowerBound, upperBound, startPoint);          // Too easy lol
	}
}

  ///////////////////////////////////////////////////////////////////////////////////////////////////
 //                                  Track a Cartesian trajectory                                 //
///////////////////////////////////////////////////////////////////////////////////////////////////
Eigen::VectorXf track_cartesian_trajectory(const Eigen::Isometry3f        &pose,
                                           const Eigen::Matrix<float,6,1> &vel,
										   const Eigen::Matrix<float,6,1> &acc)
{
	return move_endpoint(vel + this->Kc*get_pose_error(pose,get_endpoint_pose()));                  // Feedforward + feedback control
}

  ///////////////////////////////////////////////////////////////////////////////////////////////////
 //                                  Track a joint trajectory                                     //
///////////////////////////////////////////////////////////////////////////////////////////////////
Eigen::VectorXf SerialKinematicControl::track_joint_trajectory(const Eigen::VectorXf &pos,
                                                               const Eigen::VectorXf &vel,
															   const Eigen::VectorXf &acc)
{
	if(pos.size() != this->n or vel.size() != this->n or acc.size() != this->n)
	{
		std::cerr << "[ERROR] [SERIAL KINEMATIC CONTROL] track_joint_trajectory(): "
		          << "This robot has " << this->n << " joints, but "
				  << "the position argument had " << pos.size() << " elements, "
				  << "the velocity argument had " << vel.size() << " elements, "
				  << "and the acceleration argument had " << acc.size() << " elements." << std::endl;
				  
		return Eigen::VectorXf::Zero(this->n);                                                      // Don't move
	}
	else return move_joints(vel + this->kp*(pos - this->q));                                        // Feedforward + feedback control
}

  ///////////////////////////////////////////////////////////////////////////////////////////////////
 //            Compute the instantaneous acceleration bounds for joint limit avoidance            //
///////////////////////////////////////////////////////////////////////////////////////////////////
bool compute_velocity_bounds(float &lower, float &upper, const unsigned int &jointNumber)
{
	if(jointNumber >= this->n)
	{
		std::cerr << "[ERROR] [SERIAL KINEMATIC CONTROL] compute_velocity_bounds(): "
		          << "You called for " << jointNumber << ", but this robot only has "
				  << this->n << " joints." << std::endl;
				  
		return false;
	}
	else
	{
		lower = std::max( (this->posLimit[i][0] - this->q[i])*this->hertz,                          // Position limit
		        std::max( -this->velLimit[i],                                                       // Velocity limit
		                  -2*sqrt(this->maxAccel*(this->q[i] - this->posLimit[i][0]))));            // Acceleration limit

		upper = std::min( (this->posLimit[i][1] - this->q[i])*this->hertz,
		        std::min(  this->veLimit[i],
						   2*sqrt(this->maxAccel*(this->posLimit[i][1] - this->q[i]))));
		
		return true;
	}
}
