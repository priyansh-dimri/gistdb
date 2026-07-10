#include "gistdb/storage/fixed_width_column.hpp"

namespace gistdb::storage
{

    template <typename T>
    void FixedWidthColumn<T>::Append(T value)
    {
        values_.push_back(value);
        validity_.PushBack(true);
    }

    template <typename T>
    void FixedWidthColumn<T>::AppendNull()
    {
        values_.push_back(T{});
        validity_.PushBack(false);
    }

    template <typename T>
    T FixedWidthColumn<T>::GetValue(std::size_t index) const
    {
        return values_[index];
    }

    template <typename T>
    void FixedWidthColumn<T>::SetValue(std::size_t index, T value)
    {
        values_[index] = value;
        validity_.SetValid(index, true);
    }

    template <typename T>
    void FixedWidthColumn<T>::SetNull(std::size_t index, bool is_null)
    {
        validity_.SetValid(index, !is_null);
    }

    template <typename T>
    bool FixedWidthColumn<T>::IsNull(std::size_t index) const
    {
        return validity_.IsNull(index);
    }

    template <typename T>
    bool FixedWidthColumn<T>::IsValid(std::size_t index) const
    {
        return validity_.IsValid(index);
    }

    template <typename T>
    std::size_t FixedWidthColumn<T>::Size() const
    {
        return values_.size();
    }

    template <typename T>
    const T *FixedWidthColumn<T>::Data() const
    {
        return values_.data();
    }

    template <typename T>
    const ValidityBitmap &FixedWidthColumn<T>::Validity() const
    {
        return validity_;
    }

    template class FixedWidthColumn<std::int32_t>;
    template class FixedWidthColumn<float>;

} // namespace gistdb::storage