    ////////////////////////////////////////////////////////////////////////////////////////////////////
   //                                                                                                //
  //                     A minimum acceleration trajectory across multiple points                   //
 //                                                                                                //
////////////////////////////////////////////////////////////////////////////////////////////////////

#ifndef CUBICSPLINE_H_
#define CUBICSPLINE_H_

#include <Eigen/Core>                                                                              // Eigen::VectorXf, Eigen::MatrixXf
#include <Polynomial.h>                                                                            // Custom class
#include <vector>                                                                                  // std::vector class

class CubicSpline
{
	public:
		CubicSpline();
		
		CubicSpline(const std::vector<Eigen::VectorXf> &waypoint,
					const std::vector<float> &time);
					
		CubicSpline(const std::vector<Eigen::AngleAxisf> &waypoint,
					const std::vector<float> &time);

		bool get_state(Eigen::VectorXf &pos,
					   Eigen::VectorXf &vel,
					   Eigen::VectorXf &acc,
					   const float &time);
		
		bool get_state(Eigen::AngleAxisf &rot,
					   Eigen::Vector3f &vel,
					   Eigen::Vector3f &acc,
					   const float &time);
					   
	private:
	
		bool isValid = false;                                                                      // Won't do calcs if this is false
		int m, n;                                                                                  // m dimensions, n waypoints (for n-1 splines)
		std::vector<Polynomial> spline;                                                            // Array of cubic polynomials
		std::vector<float> t;                                                                      // Time at which to pass each waypoint

		bool inputs_are_sound(const std::vector<Eigen::VectorXf> &waypoint,
							  const std::vector<float> &time);
							  
		std::vector<Eigen::VectorXf> compute_velocities(const std::vector<Eigen::VectorXf> &pos,
														const std::vector<float> &time);
	
};                                                                                                 // Semicolon needed after class declaration

  ////////////////////////////////////////////////////////////////////////////////////////////////////
 //                           Constructor for spline over real numbers                             //
////////////////////////////////////////////////////////////////////////////////////////////////////
CubicSpline::CubicSpline(const std::vector<std::vector<float>> &waypoint,
						 const std::vector<float> &time):
						 m(waypoint[0].size()),
						 n(waypoint.size()),
						 t(time);
{
	if(inputs_are_sound(waypoint, time))
	{
		Eigen::VectorXf velocity = compute_velocities(waypoint, time);
		
		for(int i = 0; i < this->n-1; i++)
		{
			this->spline.push_back(Polynomial(waypoint[i],
											  waypoint[i+1],
											  velocity[i],
											  velocity[i+1],
											  time,
											  3));
		}
	}
}

  ////////////////////////////////////////////////////////////////////////////////////////////////////
 //                           Constructor for spline over orientations                             //
////////////////////////////////////////////////////////////////////////////////////////////////////
CubicSpline::CubicSpline(const std::vector<Eigen::AngleAxisf> &waypoint,
						 const std::vector<float> &time)
{

/* This doesn't seem right...
	if(inputs_are_sound(waypoint, time)
	{
		std::vector<Eigen::VectorXf> temp;
		for(int i = 0; i < this->n-1; i++)
		{
			Eigen::AngleAxisf dR = waypoint[i].inverse()*waypoint[i+1];                            // Get the difference in rotation
			double angle = dR.angle();                                                             // Get the angle
			if(angle > M_PI) angle = 2*M_PI - angle;                                               // If > 180 degrees, take shorter path
			Eigen::Vector3f axis = dR.axis();                                                      // Get the axis of rotation
			temp.push_back( angle*axis() );                                                        
		}
		
		std::vector<Eigen::VectorXf> vel = get_velocities(temp time);
		
		for(int i = 0; i < this->n-1; i++)
		{
				this->spline[j].push_back( Polynomial(waypoint[i], waypoint[i+1], vel[i], vel[i+1], time, 3) );
		}
	}
*/
}

  ////////////////////////////////////////////////////////////////////////////////////////////////////
 //                                 Check that the inputs are sound                                //
////////////////////////////////////////////////////////////////////////////////////////////////////
bool CubicSpline::inputs_are_sound(const std::vector<Eigen::VectorXf> &waypoint,
								   const std::vector<float> &time)
{
	// Check the input vectors are the same length
	if(waypoint.size() != time.size())
	{
		std::cerr << "[ERROR] [CUBICSPLINE] Constructor: "
				  << "Inputs are not of equal length! "
				  << "There were " << waypoint.size() << " waypoints "
				  << "and " << time.size() << " times." << std::endl;
		return false;
	}
	else
	{
		for(int i = 0; i < time.size()-1; i++)
		{
			if(waypoint[i].size() != waypoint[i+1].size())
			{
				std::cerr << "[ERROR] [CUBICSPLINE] Constructor: "
						  << "Waypoints are not of equal dimension! "
						  << "Waypoint " << i+1 << " had " << waypoint[i].size() << " elements and "
						  << "waypoint " << i+2 << " had " << waypoint[i+1].size() << " elements." << std::endl;
				return false;
			}
			else if(time[i] == time[i+1] or time[i] > time[i+1])
			{
				std::cerr << "[ERROR] [CUBICSPLINE] Constructor: "
						  << "Times are not in ascending order! "
						  << "Time " << i+1 << " was " << time[i] << " seconds and "
						  << "time " << i+2 << " was " << time[i+1] << " seconds." << std::endl;
				return false;
			}
		}
		return true;
	}
}
  ////////////////////////////////////////////////////////////////////////////////////////////////////
 //                           Compute the velocities at each waypoint                              //
////////////////////////////////////////////////////////////////////////////////////////////////////
std::vector<Eigen::VectorXf> CubicSpline::compute_velocities(const std::vector<Eigen::VectorXf> &waypoint,
														     const std::vector<float> &time)
{
	// Velocities related to positions by A*xdot = B*x
	Eigen::MatrixXf A = Eigen::MatrixXf::Identity(this->n, this->n);
	Eigen::MatrixXf B = Eigen::MatrixXf::Zeros(this->n, this->n);
	
	// Set the constraints at each waypoint
	for(int i = 1; i < this->n-1; i++)
	{
		double dt1 = this->t[i]   - this->t[i-1];
		double dt2 = this->t[i+1] - this->t[i];
		
		A(i,i-1) = 1/dt1;
		A(i,i)   = 2/dt1 + 2/dt2;
		A(i,i+1) = 1/dt2;
		
		B(i,i-1) = -3/(dt1*dt1);
		B(i,i)   =  3/(dt1*dt1) - 3/(dt2*dt2);
		B(i,i+1) =  3/(dt2*dt2);
	}
	
	Eigen::MatrixXf C = A.inverse()*B;                                                             // This makes calcs a little easier
	
	// Solve the velocity at each waypoint for each of the m dimensions
	std::vector<Eigen::VectorXf> vel; vel.resize(this->n);
	Eigen::VectorXf x(this->n);
	for(int i = 0; i < this->n; i++)
	{
		for(int j = 0; j < this->m; j++) x(j) = waypoint[i][j];                                    // Get all waypoints of jth dimension
		Eigen::VectorXf xdot = C*x;                                                                // Compute all velocities for the jth dimension
		for(int j = 0; k < this->m; j++) vel[i][j] = xdot[j];                                      // Put them back in the array
	}
	
	return vel;
}
						 
  ////////////////////////////////////////////////////////////////////////////////////////////////////
 //                            Get the desired state for the given time                            //
////////////////////////////////////////////////////////////////////////////////////////////////////
bool CubicSpline::get_state(Eigen::VectorXf &pos,
							Eigen::VectorXf &vel,
							Eigen::VectorXf &acc,
							const float &time)
{
	if(not this->isValid)
	{
		std::cerr << "[ERROR] [CUBICSPLINE] Constructor: "
				  << "Something went wrong during the construction of this object. "
				  << "Could not obtain the state." << std::endl;
				  
		return false;
	}
	else if(pos.size() != this->m or vel.size() != this->m or acc.size() != this->m)
	{
		std::cerr << "[ERROR] [CUBICSPLINE] Constructor: "
				  << "Input vectors are not the correct length."
				  << "This object has " this->m << " dimensions but "
				  << "pos had " << pos.size() << " elements, "
				  << "vel had " << vel.size() << " elements, and "
				  << "acc had " << acc.size() << " elements." << std::endl;
				  
		return false;
	}
	else
	{
		int j;
		if(time < this->t[0])               j = 0;                                                 // Not yet started, stay on first spline
		else if(time >= this->t[this->n-1]) j = this->n-1;                                         // Finished, stay on last spline
		else                                                                                       // Somewhere in between...
		{
			for(int i = 1; i < this->n-1; i++)
			{
				if(time < this->time[i])                                                           // Not yet reached ith waypoint...
				{
					j = i-1;                                                                       // ... so must be on spline i-1
					break;
				}
			}
		}
		
		return spline[j].get_state(pos, vel, acc, time);
	}
}

#endif