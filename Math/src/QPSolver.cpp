#include <iostream>                                                                                 // std::cerr
#include <math.h>
#include <QPSolver.h>                                                                               // Declaration of functions
#include <vector>                                                                                   // std::vector


  ///////////////////////////////////////////////////////////////////////////////////////////////////
 //                        Solve a generic QP problem min 0.5*x'*H*x - x'*f                       //
///////////////////////////////////////////////////////////////////////////////////////////////////
Eigen::VectorXf QPSolver::solve(const Eigen::MatrixXf &H,
                                const Eigen::VectorXf &f,
                                const Eigen::VectorXf &x0)
{
	// Check the inputs are sound
	int n = x0.size();
	if(H.rows() != n or H.cols() != n or f.size() != n)
	{
		std::cerr << "[ERROR] [QPSOLVER] solve(): "
		          << "Dimensions of input arguments do not match. "
		          << "H matrix was " << H.rows() << "x" << H.cols() << ", "
		          << "f vector was " << f.size() << "x1, and "
		          << "x0 vector was " << x0.size() << "x1." << std::endl;
		
		return x0;
	}
	else	return solve_linear_system(f,H,x0);                                                 // Too easy, lol
}


  ///////////////////////////////////////////////////////////////////////////////////////////////////
 //          Solve a constrained QP problem: min 0.5*x'*H*x - x'*f subject to: B*x >= c           //
///////////////////////////////////////////////////////////////////////////////////////////////////
Eigen::VectorXf QPSolver::solve(const Eigen::MatrixXf &H,                                          // Solve a QP problem with inequality constraints
                                const Eigen::VectorXf &f,
                                const Eigen::MatrixXf &B,
                                const Eigen::VectorXf &c,
                                const Eigen::VectorXf &x0)
{
	// Check the inputs are sound
	int n = x0.size();
	if(H.rows() != n or H.cols() != n or f.size() != n or B.rows() != c.size())
	{
		std::cout << "[ERROR] [QPSOLVER] solve(): "
		          << "Dimensions of input arguments do not match. "
		          << "H matrix was " << H.rows() << "x" << H.cols() << ", "
		          << "f vector was " << f.size() << "x1, "
		          << "B matrix was " << B.rows() << "x" << B.cols() << ", "
		          << "c vector was " << c.size() << "x1, and "
		          << "x0 vector was " << n << "x1." << std::endl;
		          
		return x0;
	}
	else
	{
		// Solve the following optimization problem with Guass-Newton method:
		//
		//    min f(x) = 0.5*x'*H*x - x'*f - u*sum(log(d_i))
		//
		// where d_i = b_i*x - c_i is the distance to the constraint
		//
		// Then the gradient and Hessian are:
		//
		//    g(x) = H*x - f - u*sum((1/d_i)*b_i')
		//
		//    I(x) = H + u*sum((1/(d_i^2))*b_i'*b_i)
		
		// Local variables
		bool violation;                                                                     // Flags a constraint violation
		
		Eigen::MatrixXf I;                                                                  // Hessian matrix
		Eigen::VectorXf g(n);                                                               // Gradient vector
		Eigen::VectorXf dx = Eigen::VectorXf::Zero(n);                                      // Newton step = -I\g
		Eigen::VectorXf x      = x0;                                                        // Assign initial state variable
		Eigen::VectorXf x_prev = x0;                                                        // For saving previous solution
		
		float alpha;                                                                        // Scalar for Newton step
		float beta  = this->beta0;                                                          // Shrinks barrier function
		float u     = this->u0;                                                             // Scalar for barrier function
	
		int numConstraints = B.rows();
		
		std::vector<float> d; d.resize(numConstraints);
		
		// Do some pre-processing
		std::vector<Eigen::VectorXf> bt(numConstraints);
		std::vector<Eigen::MatrixXf> btb(numConstraints);
		for(int j = 0; j < B.rows(); j++)
		{
			bt[j]  = B.row(j).transpose();                                              // Row vectors of B transposed
			btb[j] = bt[j]*bt[j].transpose();                                           // Outer product of row vectors
		}

		// Run the interior point method
		for(int i = 0; i < this->steps; i++)
		{
			// (Re)set values for new loop
			violation = false;
			g.setZero();                                                                // Gradient vector
			I = H;                                                                      // Hessian for log-barrier function
			
			// Compute distance to each constraint
			for(int j = 0; j < numConstraints; j++)
			{
				d[j] = bt[j].dot(x) - c(j);                                         // Distance to jth constraint
				
				if(d[j] < 0)
				{
					violation = true;                                           // Flag constraint violation
					d[j] = 1E-02;                                               // Set a small, non-zero value
					u *= this->uMod;                                            // Increase barrier function
				}
				
				g += -(u/d[j])*bt[j];                                               // Add up gradient vector
				I +=  (u/(d[j]*d[j]))*btb[j];                                       // Add up Hessian
			}
			
			if(violation)
			{
				x = x_prev;                                                         // Go back to last solution
				beta += this->betaMod*(1.0 - beta);                                 // Slow decrease in barrier function
			}
			
			g += H*x - f;                                                               // Finish summation of gradient vector
			
			dx = I.partialPivLu().solve(-g);                                            // Solve Newton step
			
			// Compute optimal step size
			alpha = this->alpha0;
			for(int j = 0; j < numConstraints; j++)
			{
				while( d[j] + alpha*bt[j].dot(dx) < 0) alpha *= this->alphaMod;     // Shrink step size until constraint is OK
			}

			if(alpha*dx.norm() < this->tol) break;                                      // Step size is miniscule, exit solver
			
			// Update values for next loop
			x_prev = x;                                                                 // Save value for next round
			x     += alpha*dx;                                                          // Increment state
			u     *= beta;                                                              // Decrease barrier function
		}
		
		return x;
	}
}	

  ///////////////////////////////////////////////////////////////////////////////////////////////////
 //          Solve an unconstrained least squares problem: min 0.5(y-*Ax)'*W*(y-A*x)              //
///////////////////////////////////////////////////////////////////////////////////////////////////
Eigen::VectorXf QPSolver::least_squares(const Eigen::VectorXf &y,
                                        const Eigen::MatrixXf &A,
                                        const Eigen::MatrixXf &W,
                                        const Eigen::VectorXf &x0)
{
	// Check the inputs are sound
	int m = A.rows();
	int n = A.cols();
	if(y.size() != m or x0.size() != n)
	{
		std::cerr << "[ERROR] [QPSOLVER] least_squares(): "
		          << "Dimensions of inputs arguments do not match! "
		          << "The y vector was " << y.size() << "x1, "
		          << "the A matrix was " << m << "x" << n << ", "
		          << "the W matrix was " << W.rows() << "x" << W.cols() << ", and "
		          << "the x0 vector was " << x0.size() << "x1." << std::endl;
		
		return x0;
	}
	else if(W.rows() != m or W.cols() != m)
	{
		std::cerr << "[ERROR] [QPSOLVER] least_squares(): "
		          << "Weighting matrix W was " << W.rows() << "x" << W.cols() << ", "
		          << "but expected " << m << "x" << m << "." << std::endl;
		return x0;
	}
	else
	{
		Eigen::MatrixXf AtW = A.transpose()*W;
		return solve(AtW*A,AtW*y,x0);
	}
}
  
  ///////////////////////////////////////////////////////////////////////////////////////////////////
 //    Solve a constrained least squares problem 0.5*(y-A*x)'*W*(y-A*x) s.t. xMin <= x <= xMax    //
///////////////////////////////////////////////////////////////////////////////////////////////////                           
Eigen::VectorXf QPSolver::least_squares(const Eigen::VectorXf &y,
                                        const Eigen::MatrixXf &A,
                                        const Eigen::MatrixXf &W,
                                        const Eigen::VectorXf &xMin,
                                        const Eigen::VectorXf &xMax,
                                        const Eigen::VectorXf &x0)
{
	int m = A.rows();
	int n = A.cols();
	
	// Check that the inputs are sound
	if(y.size() != m or x0.size() != n or xMin.size() != n or xMax.size() != n or x0.size() != n)
	{
		std::cerr << "[ERROR] [QPSOLVER] least_squares(): "
		          << "Dimensions of inputs arguments do not match! "
		          << "The y vector was " << y.size() << "x1, "
		          << "the A matrix was " << m << "x" << n << ", "
		          << "the W matrix was " << W.rows() << "x" << W.cols() << ", "
		          << "the xMin vector was " << xMin.size() << "x1, "
		          << "the xMax vector was " << xMax.size() << "x1, and "
		          << "the x0 vector was " << x0.size() << "x1." << std::endl;
		
		return x0;
	}
	else if(W.rows() != m or W.cols() != m)
	{
		std::cerr << "[ERROR] [QPSOLVER] least_squares(): "
		          << "Weighting matrix W was " << W.rows() << "x" << W.cols() << ", "
		          << "but expected " << m << "x" << m << "." << std::endl;
		return x0;
	}
	else
	{
		// Set up constraint matrices in standard form Bx >= c
		Eigen::MatrixXf B(2*n,n);
		B.block(0,0,n,n) = -1*Eigen::MatrixXf::Identity(n,n);
		B.block(n,0,n,n) =    Eigen::MatrixXf::Identity(n,n);
		
		Eigen::VectorXf c(2*n);
		c.head(n) = -1*xMax;
		c.tail(n) =    xMin;
		
		Eigen::MatrixXf AtW = A.transpose()*W;
		
		return solve(AtW*A, AtW*y, B, c, x0);
	}
}

  ///////////////////////////////////////////////////////////////////////////////////////////////////
 //       Solve a least squares problem of the form min 0.5*(xd-x)'*W*(xd-x)  s.t. A*x = y        //
///////////////////////////////////////////////////////////////////////////////////////////////////                                  
Eigen::VectorXf QPSolver::least_squares(const Eigen::VectorXf &xd,
                                        const Eigen::MatrixXf &W,
                                        const Eigen::VectorXf &y,
                                        const Eigen::MatrixXf &A,
                                        const Eigen::VectorXf &x0)
{
	// Check that the inputs are sound
	int m = A.rows();
	int n = A.cols();
	
	if(xd.size() != n or y.size() != m or x0.size() != n)
	{
		std::cerr << "[ERROR] [QPSOLVER] least_squares(): "
		          << "Dimensions of input arguments do not match. "
		          << "The xd vector was " << xd.size() << "x1, "
		          << "the W matrix was " << W.rows() << "x" << W.cols() << ", "
		          << "the y vector was " << y.size() << "x1, "
		          << "the A matrix was " << m << "x" << n << ", and "
		          << "the x0 vector was " << x0.size() << "x1." << std::endl;
		
		return x0;
	}
	else if(W.rows() != n or W.cols() != n)
	{
		std::cerr << "[ERROR] [QPSOLVER] least_squares(): "
		          << "Weighting matrix W was " << W.rows() << "x" << W.cols() << ", "
		          << "but expected " << n << "x" << n << "." << std::endl;
		
		return x0;
	}
	else
	{
		// Lagrangian L = 0.5*x'*W*x - x'*W*xd + (A*x - y)'*lambda,
		// Solution exists for:
		//
		// [ dL/dlambda ]  =  [ 0   A ][ lambda ] - [   y  ] = [ 0 ]
		// [   dL/dx    ]     [ A'  W ][   x    ]   [ W*xd ]   [ 0 ]
		//
		// but we can speed up calcs and skip solving lambda if we are clever.
		
		Eigen::MatrixXf H = Eigen::MatrixXf::Zero(m+n,m+n);
//		H.block(0,0,m,m) = Eigen::MatrixXf::Zero(m,n);
		H.block(0,m,m,n) = A;
		H.block(m,0,n,m) = A.transpose();
		H.block(m,m,n,n) = W;
		
		Eigen::MatrixXf Q, R;
		if(get_qr_decomposition(H,Q,R))
		{
			return backward_substitution(Q.block(0,m,m,n).transpose()*y                 //   Q12'*y
			                           + Q.block(m,m,n,n).transpose()*W*xd,             // + Q22*W*xd 
			                             R.block(m,m,n,n),                              //   R22
			                             x0);
		}
		else    return x0;
	}
}

  ///////////////////////////////////////////////////////////////////////////////////////////////////
 //     Solve a problem of the form min 0.5*(xd-x)'*W*(xd-x)  s.t. A*x = y, xMin <= x <= xMax     //
///////////////////////////////////////////////////////////////////////////////////////////////////  
                              
Eigen::VectorXf QPSolver::least_squares(const Eigen::VectorXf &xd,
                                        const Eigen::MatrixXf &W,
                                        const Eigen::VectorXf &y,
                                        const Eigen::MatrixXf &A,
                                        const Eigen::VectorXf &xMin,
                                        const Eigen::VectorXf &xMax,
                                        const Eigen::VectorXf &x0)
{
	// Check that the inputs are sound
	int m = A.rows();
	int n = A.cols();
	if(xd.size() != n or y.size() != m or x0.size() != n)
	{
		std::cerr << "[ERROR] [QPSOLVER] least_squares(): "
		          << "Dimensions of input arguments do not match. "
		          << "The xd vector was " << xd.size() << "x1, "
		          << "the W matrix was " << W.rows() << "x" << W.cols() << ", "
		          << "the y vector was " << y.size() << "x1, "
		          << "the A matrix was " << m << "x" << n << ", "
		          << "the xMin vector was " << xMin.size() << "x1, "
		          << "the xMax vector was " << xMax.size() << "x1, and "
		          << "the x0 vector was " << x0.size() << "x1." << std::endl;
		
		return x0;
	}
	else if(W.rows() != n or W.cols() != n)
	{
		std::cerr << "[ERROR] [QPSOLVER] least_squares(): "
		          << "Weighting matrix W was " << W.rows() << "x" << W.cols() << ", "
		          << "but expected " << n << "x" << n << "." << std::endl;
		
		return x0;
	}
	else
	{
		// H = [ 0  A ]
		//     [ A' W ]
		Eigen::MatrixXf H(m+n,m+n);
		H.block(0,0,m,m).setZero();
		H.block(0,m,m,n) = A;
		H.block(m,0,n,m) = A.transpose();
		H.block(m,m,n,n) = W;
		
		// f = [   y  ]
		//     [ W*xd ]
		Eigen::VectorXf f(m+n);
		f.head(m) = y;
		f.tail(n) = W*xd;
		
		// B = [ 0 -I ]
		//     [ 0  I ]
		Eigen::MatrixXf B(2*n,m+n);
		B.block(0,0,2*n,m).setZero();
		B.block(n,m,n,n).setIdentity();
		B.block(0,m,n,n) = -B.block(n,m,n,n);
		//B.block(0,m,n,n) = -Eigen::MatrixXf::Identity(n,n);
		//B.block(n,m,n,n) =  Eigen::MatrixXf::Identity(n,n);
		
		// c = [ -xMax ]
		//     [  xMin ]
		Eigen::VectorXf c(2*n);
		c.head(n) = -xMax;
		c.tail(n) =  xMin;
		
		Eigen::VectorXf state(m+n);
		state.head(m).setZero(); // NOTE: Try solving A'*lambda = W*(x-xd) as an initial guess
		state.tail(n) = x0;
	
		state = solve(H,f,B,c,state);
		
		return state.tail(n);
	}
}                  
