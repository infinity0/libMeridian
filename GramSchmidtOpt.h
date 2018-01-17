#ifndef CLASS_GRAMSCHMIDT
#define CLASS_GRAMSCHMIDT

#include "Common.h"

class GramSchmidtOpt {
private:
	double* orthVectors;
	int gsSize, numRows;

public:
	GramSchmidtOpt(int size) : gsSize(size) {
		numRows = 0;
		orthVectors = (double*)malloc(sizeof(double) * gsSize * gsSize);
	}

	int addVector(double* inVector);
	double* returnOrth(int* retRows);

	~GramSchmidtOpt() {
		free(orthVectors);
	}
};

#endif
