/******************************************************************************
Meridian prototype distribution
Copyright (C) 2005 Bernard Wong

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

The copyright owner can be contacted by e-mail at bwong@cs.cornell.edu
*******************************************************************************/

using namespace std;

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <vector>
#include "GramSchmidtOpt.h"

extern "C" {
	#include <cblas-atlas.h>
}

int GramSchmidtOpt::addVector(double* inVector) {
	if (numRows >= gsSize) {
		return 0;
	}

	cblas_dcopy(gsSize, inVector, 1, &orthVectors[numRows * gsSize], 1);

	if (numRows > 0) {
		for (int i = 0; i < numRows; i++) {
			double topValue = 
				cblas_ddot(gsSize, inVector, 1, &orthVectors[i * gsSize], 1);

			cblas_daxpy(gsSize,
				-1.0 * topValue, &orthVectors[i * gsSize], 1, 
				&orthVectors[numRows * gsSize], 1);
		}	
	}

	for (int i = 0; i < gsSize; i++) {
		if (orthVectors[numRows * gsSize + i] != 0.0) {	
			cblas_dscal(gsSize, 
				1.0 / cblas_dnrm2(gsSize, &orthVectors[numRows * gsSize], 1), 
				&orthVectors[numRows * gsSize], 1);
			numRows++;
			break;
		}
	}
	return 0;
}

double* GramSchmidtOpt::returnOrth(int* retRows) {
	*retRows = numRows;
	return orthVectors;
}

#if 0
int main() {
	GramSchmidtOpt gs(3);

	double a[3] = {1.0, -1.0, 1.0};
	double b[3] = {2.0, 2.0, -2.0};

	gs.addVector((double*)&a);
	gs.addVector((double*)&b);

	vector<double*>* vv = gs.returnOrth();

	printf("[");
	for (u_int i = 0; i < vv->size(); i++) {
		for (u_int j = 0; j < 3; j++) {
			printf("%f ", ((*vv)[i])[j]);
		}
		printf(";\n");
	}
	printf("]\n");

	double a1, a2, b1, b2;
	a1 = cblas_ddot(3, (double*)&a, 1, (*vv)[0], 1);
	a2 = cblas_ddot(3, (double*)&a, 1, (*vv)[1], 1);

	b1 = cblas_ddot(3, (double*)&b, 1, (*vv)[0], 1);
	b2 = cblas_ddot(3, (double*)&b, 1, (*vv)[1], 1);

	printf("A = [%f %f; %f %f];\n", a1, a2, b1, b2);
	return 0;
}
#endif
