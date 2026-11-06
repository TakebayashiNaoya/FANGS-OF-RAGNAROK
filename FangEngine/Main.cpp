/**
 * @file Main.cpp
 * @brief メイン関数の実装
 */
#include <cstdlib>
#include "Application.h"


int main()
{
	fangEngine::Application application;
	if (application.Initialize() == false) return EXIT_FAILURE;
	if (application.Run() == false)        return EXIT_FAILURE;

	return 0;
}
