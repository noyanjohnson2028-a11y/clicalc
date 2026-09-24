#include "clicalc/engine.hpp"
#include <iostream>
#include <stdexcept>
#include <string>

using clicalc::Engine;
using clicalc::Number;
int checks = 0;
void require(bool condition, const std::string& description) {
    ++checks; if (!condition) throw std::runtime_error(description);
}
void equal(Engine& e, const std::string& expression, const std::string& expected) {
    auto actual = e.evaluate(expression).format(10,30);
    require(actual == expected, expression+": expected "+expected+", got "+actual);
}
void near(Engine& e, const std::string& expression, const std::string& expected, const std::string& tolerance = "1e-100") {
    auto difference = e.evaluate(expression)-Number(expected);
    if (difference < Number(0)) difference = -difference;
    require(difference < Number(tolerance),expression+": outside tolerance");
}
template<class F> void fails(F action, const std::string& description) {
    bool threw = false; try { action(); } catch (const std::runtime_error&) { threw = true; }
    require(threw,"Expected error: "+description);
}
int main() {
    try {
        Engine e;
        equal(e,"4+5*4","24"); equal(e,"2^3^2","512"); equal(e,"-2^2","-4");
        equal(e,"(-2)^2","4"); equal(e,"2^-3","0.125"); equal(e,"3!^2","36");
        equal(e,"[2+3]*4","20"); equal(e,"0xFF + 0b1001","264");
        equal(e,"0b101.11","5.75"); equal(e,"0xA.F","10.9375");
        equal(e,"5M+100k","5100000"); equal(e,"3m+2u","0.003002");
        equal(e,"1.2e3 + .5","1200.5"); equal(e,"1e-3m","1e-06");
        equal(e,"7%3","1"); equal(e,"-7%3","-1"); equal(e,"5!","120");
        equal(e,"0b1100 & 0b1010","8"); equal(e,"12 @ 10","6"); equal(e,"12 | 10","14");
        equal(e,"1<<100","1.26765060022822940149670320538e+30"); equal(e,"-8>>2","-2"); equal(e,"~3","-4");
        equal(e,"1<2 && 3>=3","1"); equal(e,"2==3 || 5!=4","1"); equal(e,"!0","1");
        equal(e,"0 && (1/0)","0"); equal(e,"1 || missing(1/0)","1");
        equal(e,"log2(1024)","10"); equal(e,"LOG8(512)","3"); equal(e,"sqrt(81)","9");
        equal(e,"round(-2.5)","-3"); equal(e,"ceil(-2.2)","-2"); equal(e,"floor(-2.2)","-3");
        equal(e,"trunc(-2.2)","-2"); equal(e,"mod(5.5,2)","1.5"); equal(e,"min(3,2)+max(3,2)","5");
        near(e,"sqrt(2)^2","2"); near(e,"ln(exp(1))","1");
        near(e,"sin(pi/2)","1"); near(e,"cos(pi)","-1");
        e.execute("mode deg"); near(e,"sin(30)","0.5"); near(e,"asin(0.5)","30"); near(e,"atan2(1,1)","45");
        for (const auto& angle : {"0", "360", "-360", "720", "360000000000000000000"}) {
            require(e.evaluate(std::string("sin(")+angle+")") == Number(0), "Exact sine at full turns");
            require(e.evaluate(std::string("cos(")+angle+")") == Number(1), "Exact cosine at full turns");
            require(e.evaluate(std::string("tan(")+angle+")") == Number(0), "Exact tangent at full turns");
        }
        equal(e,"sin(90)","1"); equal(e,"sin(-90)","-1"); equal(e,"sin(270)","-1");
        equal(e,"sin(180)","0"); equal(e,"cos(180)","-1"); equal(e,"cos(-180)","-1");
        equal(e,"cos(90)","0"); equal(e,"cos(270)","0"); equal(e,"tan(180)","0");
        for (const auto& angle : {"90", "-90", "270", "450"})
            fails([&]{e.evaluate(std::string("tan(")+angle+")");}, "Undefined degree tangent");
        require(Number(0) < e.evaluate("sin(360+1e-50)"), "Do not round nearby sine to zero");
        require(e.evaluate("sin(360-1e-50)") < Number(0), "Preserve sine below full turn");
        require(e.evaluate("cos(1e-20)") < Number(1), "Do not round nearby cosine to one");
        near(e,"sin(390)","0.5"); near(e,"tan(45)","1");
        e.execute("mode rad");
        near(e,"sin(360)","0.9589157234143065","1e-16");
        require(e.execute("m=5; m+2").output.at(0) == "ans = 7","semicolon execution");
        require(e.execute("+3").output.at(0) == "ans = 10","ans addition");
        require(e.execute("--2").output.at(0) == "ans = 8","ans subtraction");
        require(e.execute("-2").output.at(0) == "ans = -2","negative literal");
        require(e.execute("m=25;").output.empty(),"trailing semicolon");
        e.execute("par(x,y)=x*y/(x+y)"); equal(e,"par(10,10)","5");
        e.execute("outer(z)=par(z,10)+m"); equal(e,"outer(10)","30");
        e.execute("zero()=42"); equal(e,"zero()","42");
        require(e.execute("list").output.size() == 3,"list functions");
        require(e.execute("vlist").output.size() == 4,"vlist constants and user variable");
        e.execute("255");
        require(e.execute("base hex").output.at(0) == "ans = 0xFF","hex redisplay");
        require(e.execute("10").output.at(0) == "ans = 0xA","decimal input in hex mode");
        require(e.execute("base 2").output.at(0) == "ans = 0b1010","binary redisplay");
        require(e.execute("base").output.at(0) == "base = 2","base query");
        require(e.execute("-5.75").output.at(0) == "ans = -0b101.11","fractional binary output");
        e.execute("base dec");
        equal(e,"10 in -> cm","25.4"); equal(e,"in -> cm","2.54");
        equal(e,"20*5 in -> cm","254"); equal(e,"70 mi/h -> m/s","31.2928");
        equal(e,"50 N-m -> millijoules","50000"); equal(e,"1 m*m -> cm^2","10000");
        equal(e,"1 m/s^2 -> cm/s^2","100"); equal(e,"1 gallon -> L","3.785411784");
        e.execute("unit furlong = 201.168 meters"); equal(e,"1 furlong -> m","201.168");
        e.execute("unit aaa = 2 furlong"); equal(e,"1 aaa -> m","402.336");
        Engine restored; restored.restore_definitions(e.definitions());
        equal(restored,"par(10,10)","5"); equal(restored,"1 aaa -> m","402.336");
        fails([&]{restored.restore_definitions("x=2");},"state accepts definitions only");
        e.execute("sigfigs 100");
        require(e.format(e.evaluate("1/7")).size() == 102,"100 significant digits");
        e.execute("sigfigs 6; format eng"); require(e.format(Number("12345")) == "12.345k","engineering format");
        e.execute("format prefix"); require(e.format(Number("0.000012345")) == "12.345u","prefix format");
        e.execute("sigfigs 2; format finance"); require(e.format(Number("12.5")) == "12.50","finance format");
        e.execute("format never; sigfigs 8"); require(e.format(Number("0.0000012345")) == "0.0000012345","fixed format");
        e.execute("format auto; sigfigs 30");
        for (const auto& expression : {"1/0","sqrt(-1)","ln(0)","(-1)!","2.5!","1.5&2","1<<-1","1<<65537","1%0","sin()","par(1)","missing","1e+","0b102","0x","1+","(1+2]","2 3","1 m -> s","1 m^101 -> m"})
            fails([&]{e.evaluate(expression);},expression);
        for (const auto& command : {"base 8","sigfigs 0","sigfigs 101","mode gradians","pi=3","del ans","f(x,x)=x","f(x,)=x","f(x)=1+","unit m = 1 s","unit bad = -1 m", "unit odd = 1 (((m^100)^100)^100)"})
            fails([&]{e.execute(command);},command);
        e.execute("x=10"); fails([&]{e.execute("x=20; 1/0");},"line rollback"); equal(e,"x","10"); equal(e,"ans","10");
        e.execute("loop(x)=loop(x)"); fails([&]{e.evaluate("loop(1)");},"recursion limit");
        fails([&]{e.evaluate(std::string(200,'(')+"1"+std::string(200,')'));},"nesting limit");
        fails([&]{e.execute(std::string(20000,'1'));},"input limit");
        e.execute("del all"); require(e.variables().size() == 3 && e.functions().empty(),"delete all preserves constants");
        auto random = e.evaluate("rand(10)"); require(!(random < Number(0)) && random < Number(10),"random bounds");
        std::cout << checks << " engine checks passed.\n";
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
