#pragma once
#include <string>   
#include <sstream>
 
class operating_message
{
private:
    bool line_break_{false};
    bool switch_{true};
    std::string message_;
public:
    ~operating_message()=default;

    constexpr operating_message(bool line_break=false)
    :
        line_break_(line_break)
    {}
    
    constexpr operator bool()const 
    { 
        return switch_; 
    }
    
    void reset()
    {
        switch_=true;
        message_="";
    }
    
    void clear_message()
    {
        message_="";
    }
    
    void set_switch_bool(bool switchs)
    { 
        switch_=switchs; 
    }
    
    bool &get_switch_bool()
    {
        return switch_;
    }
    
    operating_message& operator+=(operating_message&& other)
    {
        message_ += std::move(other.message_);
        if (!switch_) {} 
        else 
        {
            switch_ = other.switch_;
        }
        line_break_ = other.line_break_;
        return *this;
    }

    operating_message& operator+=(const operating_message& other)
    {
        message_ += other.message_;
        if (!switch_) {}
        else 
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

    std::string_view read_messge() const
    {
        return message_;
    } 
    
    template<typename... Args>
    void write_message(bool bool_, Args&&... args_)
    {
        std::ostringstream oss;
        ((oss << std::forward<Args>(args_)), ...);
        message_ += oss.str();

        if(line_break_)
        {
           message_ += '\n';
        }
        switch_ = bool_;
    }


    operating_message(operating_message&& other) noexcept= default;
    operating_message& operator=(operating_message&& other) noexcept= default;
    operating_message(const operating_message& other)noexcept= default;
    operating_message& operator=(const operating_message& other)noexcept= default;
};