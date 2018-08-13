#pragma once
#include <cstdint>
#include <vector>
#include <array>
#include <algorithm>
#include <execution>

struct EscapeData {
	void recalculateEstimatedNewSize(const std::vector<std::uint8_t>& newSource)
	{
		this->estimatedNewSize = newSource.size();
		this->estimatedNewSize += std::count(std::execution::par_unseq, newSource.begin(), newSource.end(), this->substituteCharacter);
		this->estimatedNewSize += std::count(std::execution::par_unseq, newSource.begin(), newSource.end(), this->escape);
	}

	//escape = escape escae, escape should be the ValueType appeared less frequently
	//(real) substiteCharacter = escape escape2
	//toBeEscaped = substitueCharacter
	std::uint8_t toBeEscaped;
	std::uint8_t substituteCharacter;
	std::uint8_t escape;
	std::uint8_t escape2;
	std::size_t estimatedNewSize;
};


EscapeData findBestEscape(const std::vector<std::uint8_t>& source, std::uint8_t toBeEscaped)
{
	static_assert(std::numeric_limits<std::uint8_t>::max() < std::numeric_limits<std::size_t>::max());
	static constexpr auto elementCount = static_cast<std::size_t>(std::numeric_limits<std::uint8_t>::max()) + 1;
	auto frequencies = std::array<std::size_t, elementCount>{0};
	auto accessFrequencies = [&frequencies](std::uint8_t value) -> std::size_t& {
		return frequencies[static_cast<std::size_t>(value)];
	};

	for (auto element : source)
	{
		++accessFrequencies(element);
	}

	accessFrequencies(toBeEscaped) = std::numeric_limits<std::size_t>::max();
	auto min = static_cast<std::uint8_t>(std::min_element(frequencies.begin(), frequencies.end()) - frequencies.begin());
	accessFrequencies(min) = std::numeric_limits<std::size_t>::max();
	auto second = static_cast<std::uint8_t>(std::min_element(frequencies.begin(), frequencies.end()) - frequencies.begin());
	accessFrequencies(second) = std::numeric_limits<std::size_t>::max();
	auto third = static_cast<std::uint8_t>(std::min_element(frequencies.begin(), frequencies.end()) - frequencies.begin());

	auto result = EscapeData{ toBeEscaped, min, second, third, 0 };
	result.recalculateEstimatedNewSize(source);
	return result;
}

std::vector<std::uint8_t> escape(const std::vector<std::uint8_t>& source, const EscapeData& escapeData)
{
	auto result = std::vector<std::uint8_t>{};
	result.reserve(escapeData.estimatedNewSize);
	for (auto element : source)
	{
		if (element == escapeData.toBeEscaped)
		{
			result.emplace_back(escapeData.substituteCharacter);
		}
		else if (element == escapeData.substituteCharacter)
		{
			result.emplace_back(escapeData.escape);
			result.emplace_back(escapeData.escape2);
		}
		else
		{
			result.emplace_back(element);
			if (element == escapeData.escape)
			{
				result.emplace_back(escapeData.escape);
			}
		}
	}
	result.shrink_to_fit();
	return result;
}

std::vector<std::uint8_t> unescape(const std::vector<std::uint8_t>& escaped, const EscapeData& escapeData)
{
	auto result = std::vector<std::uint8_t>{};
	result.reserve(escaped.size());
	auto escapeOn = false;
	for (auto element : escaped)
	{
		if (escapeOn)
		{
			escapeOn = false;
			if (element == escapeData.escape)
			{
				result.emplace_back(escapeData.escape);
			}
			else if (element == escapeData.escape2)
			{
				result.emplace_back(escapeData.substituteCharacter);
			}
		}
		else if (element == escapeData.escape)
		{
			escapeOn = true;
			continue;
		}
		else if (element == escapeData.substituteCharacter)
		{
			result.emplace_back(escapeData.toBeEscaped);
		}
		else
		{
			result.emplace_back(element);
		}
	}
	result.shrink_to_fit();
	return result;
}