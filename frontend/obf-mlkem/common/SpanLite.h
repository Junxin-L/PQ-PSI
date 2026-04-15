#pragma once

#include <array>
#include <cstddef>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace osuCrypto
{
	template<typename T>
	class span
	{
	public:
		span()
			: mData(nullptr)
			, mSize(0)
		{
		}

		span(T* data, std::size_t size)
			: mData(data)
			, mSize(size)
		{
		}

		template<typename U>
		span(std::vector<U>& v)
			: mData(v.data())
			, mSize(v.size())
		{
		}

		template<typename U>
		span(const std::vector<U>& v)
			: mData(v.data())
			, mSize(v.size())
		{
		}

		template<std::size_t N>
		span(std::array<typename std::remove_const<T>::type, N>& a)
			: mData(a.data())
			, mSize(N)
		{
		}

		template<std::size_t N>
		span(const std::array<typename std::remove_const<T>::type, N>& a)
			: mData(a.data())
			, mSize(N)
		{
		}

		T* data() const
		{
			return mData;
		}

		std::size_t size() const
		{
			return mSize;
		}

		T* begin() const
		{
			return mData;
		}

		T* end() const
		{
			return mData + mSize;
		}

		T& operator[](std::size_t i) const
		{
			return mData[i];
		}

		span<T> subspan(std::size_t offset, std::size_t count) const
		{
			if (offset > mSize || offset + count > mSize)
			{
				throw std::out_of_range("span::subspan");
			}
			return span<T>(mData + offset, count);
		}

	private:
		T* mData;
		std::size_t mSize;
	};
}
