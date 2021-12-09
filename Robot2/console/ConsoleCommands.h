#pragma once
#include <string>

class Environment;

class ConsoleCommands
{
	Environment* env;
public:
	ConsoleCommands();

	bool Execute(const std::string& function, const std::string& params);

	void SetEnvironment(Environment* env) { this->env = env; }
};