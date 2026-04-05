#pragma once
#include <string>   
#include <sstream>
#include <format>

class operating_message
{
private:
    bool line_break_{false};
    bool switch_{true};
    std::string message_;
public:
    ~operating_message() noexcept = default;

    constexpr operating_message(bool line_break = false) noexcept
    :
        line_break_(line_break)
    {}
    
    [[nodiscard]] constexpr operator bool() const noexcept
    { 
        return switch_; 
    }
    
    void reset() noexcept
    {
        switch_ = true;
        message_.clear();
    }
    
    void clear_message() noexcept
    {
        message_.clear();
    }
    
    void set_switch_bool(bool switchs) noexcept
    { 
        switch_ = switchs; 
    }
    
    [[nodiscard]] bool& get_switch_bool() noexcept
    {
        return switch_;
    }
    
    [[nodiscard]] const bool& get_switch_bool() const noexcept
    {
        return switch_;
    }
    
    operating_message& operator+=(std::string_view sv) noexcept
    {
        message_ += sv;
        return *this;
    }

    operating_message& operator+=(operating_message&& other) noexcept
    {
        message_ += std::move(other.message_);
        if (!switch_) [[unlikely]] {} 
        else [[likely]]
        {
            switch_ = other.switch_;
        }
        line_break_ = other.line_break_;
        return *this;
    }

    operating_message& operator+=(const operating_message& other) noexcept
    {
        message_ += other.message_;
        if (!switch_) [[unlikely]] {}
        else [[likely]]
        {
            switch_ = other.switch_;
        }
        line_break_ = other.line_break_;
        return *this;
    }
    
    friend std::ostream& operator<<(std::ostream& os, const operating_message& str)
    { 
        os << str.message_;
        return os;
    }

    [[nodiscard]] std::string_view read_messge() const noexcept
    {
        return message_;
    } 
    
    template<typename... Args>
    void write_message(bool bool_, Args&&... args_)
    {
        std::ostringstream oss;
        ((oss << std::forward<Args>(args_)), ...);
        message_ += oss.str();

        if(line_break_) [[unlikely]]
        {
           message_ += '\n';
        }
        switch_ = bool_;
    }

    void write_message(bool bool_, std::string_view sv)
    {
        message_ += sv;

        if(line_break_) [[unlikely]]
        {
           message_ += '\n';
        }
        switch_ = bool_;
    }

    template<typename... Args>
    void write_message_fmt(bool bool_, std::format_string<Args...> fmt, Args&&... args_)
    {
        message_ += std::format(fmt, std::forward<Args>(args_)...);

        if(line_break_) [[unlikely]]
        {
           message_ += '\n';
        }
        switch_ = bool_;
    }


    operating_message(operating_message&& other) noexcept = default;
    operating_message& operator=(operating_message&& other) noexcept = default;
    operating_message(const operating_message& other) noexcept = default;
    operating_message& operator=(const operating_message& other) noexcept = default;
};